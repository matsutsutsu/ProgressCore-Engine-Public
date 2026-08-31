/// @file  DroneFormationSystem.cpp
/// @brief IFormation 方式の陣形計算。registry から陣形を引いて targetPosition を計算。
#include "DroneFormationSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Attack.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Squad.h"

#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Platform/Core/FileSystem.h"

#include <json.hpp>

using namespace CCL::ECS;
using namespace CCL::AI::Formation;

namespace
{
/// @brief データ実装（JSON）を持つ陣形の一覧。
///
/// ここに並んでいるのは「形が attackProgress の関数で決まる」技だけ。
/// カミカゼ（各機が飛距離で自分から切り上げる）と小隊突撃（1つ前の機体に追従する）は
/// 個体が状態をラッチするので、データにできない＝この表に載らない。
constexpr FormationSpecAsset kSpecAssets[] = {
    {DroneFormationType::GuillotineSlam,          "GuillotineSlam"         },
    {DroneFormationType::ClapSlam,                "ClapSlam"               },
    {DroneFormationType::SideSlapWhip,            "SideSlapWhip"           },
    {DroneFormationType::TsunamiWave,             "TsunamiWave"            },
    {DroneFormationType::TsunamiWave_RL,          "TsunamiWave_RL"         },
    {DroneFormationType::MegaClapSlam_UpperFirst, "MegaClapSlam_UpperFirst"},
    {DroneFormationType::MegaClapSlam_LowerFirst, "MegaClapSlam_LowerFirst"},
};
} // namespace

const FormationSpecAsset *DroneFormationSystem::SpecAssets()
{
    return kSpecAssets;
}

int DroneFormationSystem::SpecAssetCount()
{
    return static_cast<int>(std::size(kSpecAssets));
}

std::string DroneFormationSystem::SpecAssetPath(const char *name)
{
    return std::string("Assets/Formations/") + name + ".json";
}

void DroneFormationSystem::Initialize()
{
    // このシステムが評価する陣形クラス（IFormation）を登録する。
    // 「DroneFormationSystem が動く」ということは「陣形クラスが必要」
    // ということなので、ここに書くのが最も自然。
    //
    // 陣形を追加した時も、
    // Formations_Basic.h の RegisterAll() に1行足すだけで、
    // このシステムを持つ全シーンに自動反映される。
    CCL::AI::Formation::Basic::RegisterAll();
    CCL::AI::Formation::Attack::RegisterAll(); // 攻撃陣形（縦割り等）
    CCL::AI::Formation::Squad::RegisterAll();  // 小隊陣形（5機編隊）

    LoadSpecAssets(); // 同じ型のデータ実装。既定では C++ 側が使われる

    CCL_LOG_INFO(LogCategory::AI, "[DroneFormationSystem] 陣形クラス登録完了");
}

void DroneFormationSystem::LoadSpecAssets()
{
    auto &registry = FormationRegistry::Instance();

    int loaded = 0;
    for (const auto &asset : kSpecAssets)
    {
        const std::string path = SpecAssetPath(asset.name);
        const std::string text = FileSystem::ReadAllText(path);
        if (text.empty()) continue; // アセットが無くても C++ 実装で動く

        try
        {
            FormationSpec spec = nlohmann::json::parse(text).get<FormationSpec>();
            registry.RegisterSpec(asset.type, spec, path);
            ++loaded;
        }
        catch (const std::exception &e)
        {
            CCL_LOG_ERROR(LogCategory::AI, "[FormationSpec] 読み込み失敗 %s: %s", path.c_str(), e.what());
        }
    }

    CCL_LOG_INFO(LogCategory::AI, "[FormationSpec] データ実装 %d/%d 件を登録", loaded, SpecAssetCount());
}

FormationContext DroneFormationSystem::MakeContext(float dt) const
{
    FormationContext ctx;
    ctx.time = m_elapsed;
    ctx.dt   = dt; // 攻撃の進行度（ヒンジ回転など）を進めるのに使う

    // ボス位置（BossCommandComponent を持つ最初のEntity）
    {
        auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (auto *t = m_world->GetComponent<TransformComponent>(b))
                ctx.bossPos = t->position;
            break;
        }
    }

    // プレイヤー位置（BattleContext から）
    if (m_world->HasResource<BattleContext>())
    {
        auto &battle  = m_world->GetResource<BattleContext>();
        ctx.playerPos = battle.playerPos;
    }

    // 小隊の頭の位置（SquadUpdateSystem が今フレーム更新済み）。
    //   ここでは読むだけ。書き手を1つに固定しておくと、並列化しても壊れない。
    if (m_world->HasResource<SquadRegistry>())
        ctx.squads = &m_world->GetResource<SquadRegistry>();

    return ctx;
}

void DroneFormationSystem::Update(float dt)
{
    ZoneScopedN("DroneFormationSystem::Update");

    m_elapsed += dt;

    auto                  &registry = FormationRegistry::Instance();
    const FormationContext ctx      = MakeContext(dt);

    // --- 各ドローンの目標座標を計算 ---
    //   ポリモーフィズムは「陣形を引くとき」だけ。ループ自体は連続アクセス。
    ForEach([&](DroneComponent &drone) {
        IFormation *formation = registry.Get(drone.currentFormation);
        if (!formation) return; // 未登録の陣形（Hidden等）は何もしない

        formation->Calculate(drone, ctx); // targetPosition を書き込む
    });
}

REGISTER_LOGIC_SYSTEM(DroneFormationSystem, Priority::LogicStage::L02_Update)
