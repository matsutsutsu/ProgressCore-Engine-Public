// IFormation 方式の陣形計算。registry から陣形を引いて targetPosition を計算。
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

#include <algorithm>

using namespace CCL::ECS;
using namespace CCL::AI::Formation;

namespace
{
// 予告の溜めの上限 [秒]。振幅は移動層で頭打ちになるので、値が際限なく育たないための蓋。
constexpr float kWindupTimerCap = 10.0f;
} // namespace

void DroneFormationSystem::Initialize()
{
    // このシステムが評価する陣形クラスを登録する。陣形を追加したときは
    // 各 Formations_*.h の RegisterAll() に1行足せば全シーンへ自動反映される。
    CCL::AI::Formation::Basic::RegisterAll();
    CCL::AI::Formation::Attack::RegisterAll(); // 攻撃陣形（縦割り等）
    CCL::AI::Formation::Squad::RegisterAll();  // 小隊陣形（5機編隊）

    CCL_LOG_INFO(LogCategory::AI, "[DroneFormationSystem] 陣形クラス登録完了");
}

FormationContext DroneFormationSystem::MakeContext(float dt)
{
    FormationContext ctx;
    ctx.time = m_elapsed;
    ctx.dt   = dt; // 攻撃の進行度（ヒンジ回転など）を進めるのに使う

    // 「今撃て」信号。狙いを焼くタイミングの判定に使う。ボス位置とあわせて
    // BossCommandComponent を持つ最初のEntityから取る。
    bool attackActive = false;

    {
        auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (auto *t = m_world->GetComponent<TransformComponent>(b))
                ctx.bossPos = t->GetWorldPosition();   // position はローカル。陣形の中心はワールドで置く
            if (auto *c = m_world->GetComponent<BossCommandComponent>(b))
                attackActive = c->droneAttackActive;
            break;
        }
    }

    // プレイヤーが居ないフレームは playerPos を配らない（既定のまま＝原点を狙わせない）。
    if (m_world->HasResource<BattleContext>())
    {
        auto &battle = m_world->GetResource<BattleContext>();
        if (battle.HasPlayer())
        {
            ctx.playerPos      = battle.playerPos;
            ctx.playerCenter   = battle.playerCenter;
            ctx.playerVelocity = battle.playerVelocity;
        }
    }

    // 小隊の頭（SquadUpdateSystem が今フレーム更新済み）。ここでは読むだけ。
    if (m_world->HasResource<SquadRegistry>())
        ctx.squads = &m_world->GetResource<SquadRegistry>();

    // 狙う向きを「撃つと決めた瞬間」に1回だけ焼く。予備動作の間は追い続ける
    // （形がプレイヤーを向くからこそ、どちらへ避けるかを読める）。信号が立った
    // 瞬間から先は動かさず、避けた者勝ちにする。
    if (attackActive && !m_prevAttackActive)
    {
        DirectX::SimpleMath::Vector3 aim = ctx.playerPos - ctx.bossPos;
        aim.y = 0.0f;
        if (aim.LengthSquared() < 0.001f) aim = DirectX::SimpleMath::Vector3::UnitZ;
        aim.Normalize();

        m_lockedAimDir = aim;
        m_aimLocked    = true;
    }
    else if (!attackActive)
    {
        m_aimLocked = false;
    }
    m_prevAttackActive = attackActive;

    ctx.aimLocked = m_aimLocked;
    ctx.aimDir    = m_lockedAimDir;

    return ctx;
}

void DroneFormationSystem::Update(float dt)
{
    ZoneScopedN("DroneFormationSystem::Update");

    m_elapsed += dt;

    auto &registry = FormationRegistry::Instance();

    const FormationContext ctx = MakeContext(dt);

    // ポリモーフィズムは「陣形を引くとき」だけ。ループ自体は連続アクセス。
    ForEach([&](DroneComponent &drone) {
        IFormation *formation = registry.Get(drone.currentFormation);
        if (!formation)
        {
            // 未登録の陣形（Hidden等）は何もしない。溜めだけは戻す（抱えたままだと
            // 次に出てきた瞬間から全開で震える）。
            drone.windupTimer = 0.0f;
            return;
        }

        formation->Calculate(drone, ctx); // targetPosition を書き込む

        // 予告（震え）の溜めを進める。技であり、持ち場に着いていて、まだ撃つ合図が
        // 来ていない——この3つが揃う時間帯が予備動作そのもの。
        //   hasReachedSlot は「一度でも着いたか」のラッチなので陣形を切り替えた直後も
        //   true のままで、溜めは BT が陣形を指示した時点から始まって Wait(予告) の
        //   長さと一致する。突撃から帰る途中だけ false に落ちるので帰りながらは震えない。
        const bool telegraphing =
            formation->UsesShiverTelegraph() && drone.hasReachedSlot && !drone.wantsAttack;

        drone.windupTimer = telegraphing
                                ? (std::min)(kWindupTimerCap, drone.windupTimer + dt)
                                : 0.0f;
    });
}

REGISTER_LOGIC_SYSTEM(DroneFormationSystem, Priority::LogicStage::L02_Update)
