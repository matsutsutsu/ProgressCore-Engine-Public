#pragma once
// ===========================================================================
// HealthSystem.h
// --------------
// 役割:
//   HPの無敵時間更新と死亡判定を行う「管理」システム。
//   エディタではOnGuiで全エンティティのHP状態をリアルタイム表示する。
//
// 責務:
//   - 無敵タイマー(invincibilityTimer)の毎フレーム減算。
//   - HP0以下のエンティティへの DeadTag 付与（死亡フラグ立て）。
//   - [Editor] OnGui で HealthComponent 所持全エンティティのHP/無敵状態を一覧表示。
//
// アーキテクチャ上の位置付け(戦闘パイプライン):
//   [Jolt] 物理衝突 → EventBus
//     │
//   L05 HitboxCollisionSystem  (判定・DamageEvent生成)
//     ▼
//   L06 DamageResolutionSystem (HP減算・ノックバック付与)
//     ▼
//   L06 HealthSystem           ← 本ファイル(無敵更新・死亡判定)
//
// 設計方針:
//   - 「誰がダメージを与えたか」を知らない。HPの最終状態だけを見て判定する。
//     毒/落下/罠など将来のダメージソースが増えても本Systemは無変更で動く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Time/TimeState.h"

class HealthSystem : public CCL::ECS::IfSystem<HealthSystem,
                                               CCL::ECS::Write<HealthComponent>,
                                               CCL::ECS::Read<TimeStateComponent>>
{
public:
    HealthSystem() : IfSystem("HealthSystem")
    {
    }

    void Update(float dt) override;

    // 全エンティティのHP状態をImGuiで一覧表示する。
    void OnGui() override;
};
