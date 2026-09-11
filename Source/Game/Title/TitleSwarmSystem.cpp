// 役割・設計方針は TitleSwarmSystem.h を参照。
#include "TitleSwarmSystem.h"

#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Core/Components/PrefabInstanceComponent.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h" // HighRing（本編の頭上円陣の値）

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
// ゲーム本編と同じドローン。見た目を共有するのが要点で、タイトルだけ別物にすると
// 「この群れが操作対象なのか」が伝わらなくなる。
constexpr const char *kDronePrefab = "Assets/Prefabs/Enemy/Drone.json";

// 輪の中心を決めるために探すボス像の名前。像を動かせば輪も追従する。
constexpr const char *kBossName = "TitleBossStatue";

// 速さの個体差。全機同速だと回る輪が完全な剛体に見えるので、追従に個体差を付ける。
// 既存の moveSpeed（Idle の追従速度上限にそのまま効く）へ掛ける倍率の幅。
constexpr float kSpeedSpreadMin = 0.90f;
constexpr float kSpeedSpreadMax = 1.10f;
} // namespace

std::vector<TypeID> TitleSwarmSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<TransformComponent>::ID()); // 生成直後の座標を書く
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
    // 覚えていた像が消えていたら忘れる（シーンを跨いだ場合など）。
    if (m_bossID != InvalidEntityID && !m_world->IsEntityValid(m_bossID))
        m_bossID = InvalidEntityID;

    // 見つかるまでは毎フレーム探す。プレハブ生成は遅延なので、初回の数フレームは
    // まだ居ない可能性がある（走査は Name+Transform の1周だけなので安い）。
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
    // 本編の頭上円陣と同じ式。半径・高さ・回転速度は HighRing 側の定数に従う。
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
            // 生成フレームのうちに worldMatrix を焼く。Jolt の剛体は生成時の
            // worldMatrix を見るので、これが無いと当たりだけ原点に置かれる。
            trans->UpdateMatrix();
        }

        if (auto *drone = m_world->GetComponent<DroneComponent>(id))
        {
            // 陣形計算には触らせない。Hidden は未登録なので DroneFormationSystem が素通りする。
            drone->currentFormation = DroneFormationType::Hidden;

            // 通し番号がそのまま輪の上の位置になる。タイトルにはボスが居ないので
            // DroneManagerSystem が番号を振り直すことはない。
            drone->localIndex  = i;
            drone->totalDrones = kDroneCount;

            // 小隊。既存の SlotLag（後ろの機ほど追従が鈍る）へ効いて追従の個体差になる。
            drone->squadId     = static_cast<uint8_t>(i / kSquadSize);
            drone->slotInSquad = static_cast<uint8_t>(i % kSquadSize);

            drone->moveSpeed *= kSpeedSpreadMin +
                (kSpeedSpreadMax - kSpeedSpreadMin) * Hash01(i * 37u + 41u);

            // 生成位置がそのまま持ち場。到着済みで始めるので、開幕から輪が完成している。
            drone->targetPosition = slot;
            drone->hasReachedSlot = true;

            // 機首は輪の外向き（本編の HighOrbitFormation と同じ）。
            Vector3 look = slot - bossPos;
            look.y       = 0.0f;
            if (look.LengthSquared() > 0.001f)
            {
                look.Normalize();
                drone->targetLookDirection = look;
            }

            // ホバーの位相を散らす。揃っていると群れが1枚の板として揺れる。
            drone->hoverTimeOffset = Hash01(i * 17u + 23u) * kHoverPhasePeriod;
        }

        if (auto *time = m_world->GetComponent<TimeStateComponent>(id))
            time->group = TimeGroup::Enemy;

        // プレハブ参照を消す。ここを残したままエディタで File > Save Level すると、
        // 演出用に湧かせた機体が Title.level.json へ配置として書き込まれてしまう
        //（空パスなら、レベルの参照配置にもステージのベイクにも入らない）。
        if (auto *inst = m_world->GetComponent<PrefabInstanceComponent>(id))
            inst->prefabPath.clear();

        ++spawned;
    }

    // 生成は遅延で積まれる。ここで確定させないと、この後の ForEach が新規機を拾えない。
    m_world->ScrutinyAndApply();

    CCL_LOG_INFO(LogCategory::Game, "[TitleSwarm] タイトル演出用に %d 機を配置", spawned);
}

void TitleSwarmSystem::Update(float dt)
{
    // 輪の中心はボス像。レベル読み込み直後の数フレームは居ないことがあるので待つ。
    Vector3 bossPos;
    if (!ResolveBossPos(bossPos)) return;

    if (!m_spawned)
    {
        SpawnSwarm(bossPos);
        m_spawned = true;
        return; // 生成したフレームはそのまま見せる（すでに持ち場に居る）
    }

    m_elapsed += dt;

    ForEach([&](DroneComponent &drone) {
        const uint16_t index = drone.localIndex;
        if (index >= kDroneCount) return;

        const Vector3 slot  = SlotPosition(bossPos, index);
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

    // 広さは本編の HighRing に従う。変えたい場合は Formations_Basic.h を触る
    //（＝ゲーム中の頭上円陣も一緒に変わる。それが狙いなので値を分けていない）。
    ImGui::SeparatorText("本編の値（Formations_Basic.h の HighRing）");
    ImGui::Text("半径 %.1fm / 高さ %.1fm / 回転 %.2frad/s", HighRing::kRadius, HighRing::kHeight,
        HighRing::kSpin);

    const float gap = DirectX::XM_2PI * HighRing::kRadius / static_cast<float>(kDroneCount);
    ImGui::Text("機体間隔 %.2fm（機体の見た目は約3m）%s", gap,
        (gap < 3.0f) ? "  ← 隣と重なる" : "");
#endif
}
