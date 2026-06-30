#include "ProtoCombatSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Game/Prototype/PrototypeConfig.h"
#include "Game/Logics/GameEvent/GameEvents.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include <vector>
#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void ProtoCombatSystem::Initialize()
{
    // ジャスト回避成功イベントをフックし、ドローンを粉砕してコンボを加算する
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        if (!_world->HasResource<PrototypeConfig>()) return;
        auto &config = _world->GetResource<PrototypeConfig>();

        // 1. コンボ加算
        config.currentCombo++;
        if (config.currentCombo > config.maxCombo)
        {
            config.maxCombo = config.currentCombo;
        }

        // 2. プレイヤー座標の取得
        auto *playerTrans = _world->GetComponent<TransformComponent>(ev.playerID);
        if (!playerTrans) return;

        // 3. 撃破範囲内にいるドローンを収集し、距離順にソートして近い順に破壊
        struct DroneTarget
        {
            EntityID id;
            float    distSq;
        };
        std::vector<DroneTarget> targets;

        ForEachWithID([&](EntityID id, const TransformComponent &trans, const ProtoDroneComponent &) {
            float distSq = Vector3::DistanceSquared(playerTrans->position, trans.position);
            if (distSq <= (config.killRadius * config.killRadius))
            {
                targets.push_back({id, distSq});
            }
        });

        std::sort(targets.begin(), targets.end(), [](const DroneTarget &a, const DroneTarget &b) {
            return a.distSq < b.distSq;
        });

        // 4. 指定数だけ破壊 (Destroy)
        int killCount = 0;
        for (const auto &target : targets)
        {
            if (killCount >= config.killCountPerEvade) break;

            _world->Destroy(target.id);
            killCount++;
            config.totalKills++;
        }
    });
}

void ProtoCombatSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<PrototypeConfig>()) return;
    auto &config = _world->GetResource<PrototypeConfig>();

    // プレイヤーの座標とダッシュ状態を取得
    Vector3 playerPos;
    bool    isInvincible = false;
    bool    playerFound  = false;

    auto playerView = _world->View<PlayerComponent, DashComponent, TransformComponent>();
    for (auto p : playerView)
    {
        auto *trans  = _world->GetComponent<TransformComponent>(p);
        auto *dash   = _world->GetComponent<DashComponent>(p);
        playerPos    = trans->position;
        isInvincible = dash->isInvincible;
        playerFound  = true;
        break;
    }

    if (!playerFound) return;

    // ダッシュ中(無敵中)なら被弾判定をスキップ
    if (isInvincible) return;

    // ドローンとの接触判定（被弾）
    bool isHitThisFrame = false;
    ForEach([&](const TransformComponent &trans, const ProtoDroneComponent &drone) {
        if (isHitThisFrame) return; // 既に被弾処理済みならスキップ

        // ドローンとプレイヤーの距離（当たり判定は仮で 半径1.0f とする）
        float distSq = Vector3::DistanceSquared(playerPos, trans.position);
        if (distSq < (1.0f * 1.0f))
        {
            isHitThisFrame = true;
        }
    });

    // 被弾した場合のペナルティ
    if (isHitThisFrame)
    {
        if (config.currentCombo > 0)
        {
            // コンボリセット
            config.currentCombo = 0;
            config.hitCount++;
        }
    }
}

REGISTER_LOGIC_SYSTEM(ProtoCombatSystem, Priority::LogicStage::L05_Collision);