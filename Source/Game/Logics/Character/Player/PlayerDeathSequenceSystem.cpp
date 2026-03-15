#include "PlayerDeathSequenceSystem.h"
#include "Game/Logics/Character/Player/PlayerDeathSequenceComponent.h"
#include "Game/Logics/GameEvent/GameOverResource.h"

// --- 演出に必要なコンポーネント群 ---
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Camera/VirtualCameraComponents.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Graphics/Lighting/FogComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include <algorithm>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

PlayerDeathSequenceSystem::PlayerDeathSequenceSystem() : IfSystem("PlayerDeathSequenceSystem") {}

void PlayerDeathSequenceSystem::Update(float dt)
{
    // イテレーション中のコンポーネント削除を避けるための遅延バッファ
    std::vector<EntityID> completedEntities;

    ForEachWithID([&](EntityID id, const PlayerComponent &player, const Tag::DeadTag &dead) {
        // 1. 初回フレーム: 状態管理コンポーネントをアタッチ
        if (!_world->HasComponent<PlayerDeathSequenceComponent>(id)) {
            _world->AddComponent<PlayerDeathSequenceComponent>(id);
        }

        auto *seq = _world->GetComponent<PlayerDeathSequenceComponent>(id);
        if (!seq) return;

        // 2. ステートマシンによる演出の時系列制御
        switch (seq->phase) {
        case PlayerDeathPhase::InitialShock:
            ApplyBigShake(3.0f, 1.5f); // 激震開始
            seq->stateTimer = 1.5f;
            seq->phase      = PlayerDeathPhase::WaitShake;
            break;

        case PlayerDeathPhase::WaitShake:
            seq->stateTimer -= dt;
            if (seq->stateTimer <= 0.0f) {
                TriggerPlayerDeathEffect(id); // アニメーションとズームカメラ生成
                seq->stateTimer = 3.0f;
                seq->phase      = PlayerDeathPhase::ZoomAndFog;
            }
            break;

        case PlayerDeathPhase::ZoomAndFog:
            UpdateDeathFog(dt, 3.0f, seq->stateTimer); // フォグの補間進行
            seq->stateTimer -= dt;
            if (seq->stateTimer <= 0.0f) {
                seq->phase = PlayerDeathPhase::Completed;
            }
            break;

        case PlayerDeathPhase::Completed:
            if (_world->HasResource<GameOverResource>()) {
                auto &goRes       = _world->GetResource<GameOverResource>();
                goRes.showScoreUI = true; // UI表示トリガー
            }
            // 完了したエンティティを記録（後でTag::DeadTagを外して更新対象から除外）
            completedEntities.push_back(id);
            break;
        }
    });

    // 3. 演出が完了したプレイヤーから Tag::DeadTag を剥奪し、このシステムの処理対象から外す
    for (auto id : completedEntities) {
        _world->RequestRemoveComponent<Tag::DeadTag>(id);
    }
}

// ===================================================================================
// 以下、旧 DamageSystem からカプセル化された内部メソッド
// ===================================================================================

void PlayerDeathSequenceSystem::ApplyBigShake(float strength, float duration)
{
    auto vCams = _world->View<VirtualCamera>();
    for (auto camID : vCams) {
        auto *vcam = _world->GetComponent<VirtualCamera>(camID);
        if (!vcam || vcam->priority < 10) continue;

        auto *shake = _world->GetComponent<CameraShake>(camID);

        if (shake) {
            if (strength > shake->amplitude) {
                shake->SetShake(strength, duration);
            }
        }
        else {
            CameraShake newShake;
            newShake.SetShake(strength, duration);
            _world->AddComponent<CameraShake>(camID, newShake);
        }
    }
}

void PlayerDeathSequenceSystem::TriggerPlayerDeathEffect(EntityID playerID)
{
    // --- 1. アニメーションの再生 ---
    auto *animator  = _world->GetComponent<AnimatorComponent>(playerID);
    auto *modelComp = _world->GetComponent<ModelComponent>(playerID);

    if (animator && modelComp && modelComp->modelPtr) {
        int deathAnimIndex = modelComp->modelPtr->GetAnimationIndex("Death");
        if (deathAnimIndex != -1) {
            animator->Play(deathAnimIndex, false, 1.0f, 0.2f, true);
        }
    }

    // --- 2. デスカメラのPrefab生成とターゲット追従設定 ---
    EntityID cameraEntity =
        Prefab::SpawnPrefab(*_world, "Assets/GamePlay/Prefabs/Camera/DeathCamera");

    _world->PatchComponent<CameraBodyFollow>(
        cameraEntity, [playerID](CameraBodyFollow &cam) { cam.target = playerID; });

    _world->PatchComponent<CameraAimLookAt>(
        cameraEntity, [playerID](CameraAimLookAt &cam) { cam.target = playerID; });
}

void PlayerDeathSequenceSystem::UpdateDeathFog(float dt, float totalTime, float currentTime)
{
    // 進行度 (0.0 -> 1.0) の計算
    // float t = 1.0f - (std::max)(0.0f, currentTime / totalTime); // 線形補間で厳密にやる場合に使用

    auto view = _world->View<FogComponent>();
    for (auto entity : view) {
        auto *fog = _world->GetComponent<FogComponent>(entity);

        float targetStart = 2.0f;
        float targetEnd   = 6.0f;
        float lerpFactor  = 2.0f * dt;

        fog->start = fog->start + (targetStart - fog->start) * lerpFactor;
        fog->end   = fog->end + (targetEnd - fog->end) * lerpFactor;
    }
}


REGISTER_LOGIC_SYSTEM(PlayerDeathSequenceSystem, Priority::LogicStage::Cleanup);