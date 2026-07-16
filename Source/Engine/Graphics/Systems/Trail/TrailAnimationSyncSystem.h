#pragma once
// ===========================================================================
// TrailAnimationSyncSystem.h
// --------------------------
// 役割:
//   animControlled な TrailComponent を主役に走査し、その「Animator を持つ祖先」の
//   Trail 区間イベントを読んで emitting を毎フレーム同期する。
//
// 設計方針（CombatAnimationSyncSystem と同じイテレーション反転）:
//   - Write<TrailComponent> を宣言し、自エンティティの Trail のみ書き込む。
//     祖先の Animator は別エンティティへの読み取り専用参照（GetComponent）。
//   - 武器は BoneAttachment 等でキャラの子孫にぶら下がるため、親を辿って
//     最初に見つかった Animator 保持エンティティを owner として一度だけキャッシュする。
//     ＝インスペクタでの手動オーナー指定が不要。
//   - 本システムは L05_Collision。AnimationSystem(L02_PostUpdate) が currentTimer を
//     進めた後・描画の TrailSystem(R08) が読む前に走るため、同フレームで整合する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"

/// @brief Trail アニメイベント区間で TrailComponent::emitting を駆動するシステム。
class TrailAnimationSyncSystem
    : public CCL::ECS::IfSystem<TrailAnimationSyncSystem,
          CCL::ECS::Read<TransformComponent>,
          CCL::ECS::Write<TrailComponent>>
{
public:
    TrailAnimationSyncSystem() : IfSystem("TrailAnimationSyncSystem") {}

    void Update(float dt) override;
};
