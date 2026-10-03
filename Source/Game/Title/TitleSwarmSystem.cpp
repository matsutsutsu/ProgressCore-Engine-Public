#include "TitleSwarmSystem.h"

#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Core/Components/PrefabInstanceComponent.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h" // HighRing

#include <cmath>
#include <cstring>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

using HighRing = CCL::AI::Formation::Basic::HighRing;

namespace
{
    // ゲーム本編と同じドローンを使う。タイトルだけ見た目を変えると、
    // 本編で戦う相手だと伝わらなくなる
    constexpr const char *kDronePrefab = "Assets/Prefabs/Enemy/Drone.json";

    // 輪の中心を決めるために探すボス像の名前。像を動かせば輪も追従する。
    constexpr const char *kBossName = "TitleBossStatue";

    // 速さの個体差。全機が同じ速さだと、回る輪が1つの固い物に見える。
    // moveSpeed（待機中の追従の最高速）に掛ける倍率の範囲
    constexpr float kSpeedSpreadMin = 0.90f;
    constexpr float kSpeedSpreadMax = 1.10f;
} // namespace

std::vector<TypeID> TitleSwarmSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<TransformComponent>::ID()); // 生成した直後に座標を書く
    return types;
}

float TitleSwarmSystem::Hash01(uint32_t seed)
{
    seed ^= seed >> 15;
    seed *= 2246822519u;
    seed ^= seed >> 13;
    seed *= 3266489917u;
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

bool TitleSwarmSystem::ResolveBossPos(Vector3 &out)
{
    // 覚えていた像が消えていたら忘れる（シーンを切り替えたときなど）
    if (m_bossID != InvalidEntityID && !m_world->IsEntityValid(m_bossID))
        m_bossID = InvalidEntityID;

    // 見つかるまでは毎フレーム探す。プレハブの生成は後回しにされるので、最初の数フレームは
    // まだ無いことがある（Name と Transform を持つエンティティを1周するだけなので軽い）
    if (m_bossID == InvalidEntityID)
    {
        m_world->ViewEach<NameComponent, TransformComponent>([&](EntityID id) {
            if (m_bossID != InvalidEntityID) return;

            const auto *name = m_world->GetComponent<NameComponent>(id);
            if (name && std::strcmp(name->name, kBossName) == 0) m_bossID = id;
        });
    }

    if (m_bossID == InvalidEntityID) return false;

    const auto *trans = m_world->GetComponent<TransformComponent>(m_bossID);
    if (!trans) return false;

    out = Vector3(trans->position);
    return true;
}

Vector3 TitleSwarmSystem::SlotPosition(const Vector3 &bossPos, uint16_t index) const
{
    // 本編の頭上の円陣と同じ式。半径・高さ・回転の速さは HighRing の定数で決まる
    return bossPos + HighRing::SlotOffset(index, kDroneCount, m_elapsed);
}

void TitleSwarmSystem::SpawnSwarm(const Vector3 &bossPos)
{
    int spawned = 0;

    for (uint16_t i = 0; i < kDroneCount; ++i)
    {
        const EntityID id = Prefab::SpawnPrefab(*m_world, kDronePrefab);
        if (id == InvalidEntityID) continue;

        const Vector3 slot = SlotPosition(bossPos, i);

        if (auto *trans = m_world->GetComponent<TransformComponent>(id))
        {
            trans->position = slot;
            trans->isDirty  = true;
            // 生成したフレームのうちに worldMatrix を求めておく。Jolt の剛体は生成時の
            // worldMatrix を見るので、これが無いと当たり判定だけ原点に置かれる
            trans->UpdateMatrix();
        }

        if (auto *drone = m_world->GetComponent<DroneComponent>(id))
        {
            // 陣形の計算から外す。None には陣形が登録されていないので DroneFormationSystem が何もしない
            drone->currentFormation = DroneFormationType::None;

            // 通し番号がそのまま輪の上の位置になる。タイトルにはボスが居ないので、
            // DroneManagerSystem が番号を振り直すことはない
            drone->localIndex  = i;
            drone->totalDrones = kDroneCount;

            // 小隊を振る。SlotLag（小隊の後ろの機ほど追従が遅れる）が効いて、追従に個体差が出る
            drone->squadId     = static_cast<uint8_t>(i / kSquadSize);
            drone->slotInSquad = static_cast<uint8_t>(i % kSquadSize);

            drone->moveSpeed *= kSpeedSpreadMin +
                                (kSpeedSpreadMax - kSpeedSpreadMin) * Hash01(i * 37u + 41u);

            // 生成した位置がそのまま持ち場。到着済みで始めるので、最初から輪ができている
            drone->targetPosition = slot;
            drone->hasReachedSlot = true;

            // 機首は輪の外向き（本編の HighOrbitFormation と同じ）
            Vector3 look = slot - bossPos;
            look.y       = 0.0f;
            if (look.LengthSquared() > 0.001f)
            {
                look.Normalize();
                drone->targetLookDirection = look;
            }

            // ホバーの位相をばらす。そろっていると群れ全体が1枚の板のように揺れる
            drone->hoverTimeOffset = Hash01(i * 17u + 23u) * kHoverPhasePeriod;
        }

        if (auto *time = m_world->GetComponent<TimeStateComponent>(id))
            time->group = TimeGroup::Enemy;

        // プレハブの参照を消す。残したままエディタで File > Save Level すると、
        // 演出用に生成した機体が Title.level.json に配置として書き込まれてしまう
        // （パスが空なら、レベルのプレハブ配置にもステージのベイクにも入らない）
        if (auto *inst = m_world->GetComponent<PrefabInstanceComponent>(id))
            inst->prefabPath.clear();

        ++spawned;
    }

    // 生成は後回しにされる。ここで反映しないと、この後の ForEach が新しい機体を拾えない
    m_world->ScrutinyAndApply();

    CCL_LOG_INFO(LogCategory::Game, "[TitleSwarm] タイトル演出用に %d 機を配置", spawned);
}

void TitleSwarmSystem::Update(float dt)
{
    // 輪の中心はボス像。レベルを読み込んだ直後の数フレームは無いことがあるので待つ
    Vector3 bossPos;
    if (!ResolveBossPos(bossPos)) return;

    if (!m_spawned)
    {
        SpawnSwarm(bossPos);
        m_spawned = true;
        return; // 生成したフレームはそのまま見せる（もう持ち場にいる）
    }

    m_elapsed += dt;

    ForEach([&](DroneComponent &drone) {
        const uint16_t index = drone.localIndex;
        if (index >= kDroneCount) return;

        const Vector3 slot   = SlotPosition(bossPos, index);
        drone.targetPosition = slot;

        Vector3 look = slot - bossPos;
        look.y       = 0.0f;
        if (look.LengthSquared() > 0.001f)
        {
            look.Normalize();
            drone.targetLookDirection = look;
        }
    });
}

void TitleSwarmSystem::OnGui()
{
#ifdef ENABLE_EDITOR
    ImGui::TextUnformatted("タイトルの群れ（ボス頭上の円陣）");
    ImGui::Text("機数 %d / 生成 %s / ボス像 %s", kDroneCount, m_spawned ? "済" : "未",
                (m_bossID != InvalidEntityID) ? "検出" : "未検出");

    // 広さは本編の HighRing で決まる。変えるなら Formations_Basic.h を直す
    // （ゲーム中の円陣も一緒に変わる。そうなるよう値を分けていない）
    ImGui::SeparatorText("本編の値（Formations_Basic.h の HighRing）");
    ImGui::Text("半径 %.1fm / 高さ %.1fm / 回転 %.2frad/s", HighRing::kRadius, HighRing::kHeight,
                HighRing::kSpin);

    const float gap = DirectX::XM_2PI * HighRing::kRadius / static_cast<float>(kDroneCount);
    ImGui::Text("機体間隔 %.2fm（機体の見た目は約3m）%s", gap,
                (gap < 3.0f) ? "  ← 隣と重なる" : "");
#endif
}
