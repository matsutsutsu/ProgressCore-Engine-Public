#pragma once
// ===========================================================================
// 概要: プレイヤーのジャンプ・ダッシュ・ジャスト回避に合わせて、その場に VFX を出す System
//
// 責務:
//   - PlayerJumpedEvent を受け、1段目なら VFX_GroundJump、2段目なら VFX_Jump を出す
//   - PlayerDashedEvent を受け、VFX_Dash を出し、子エンティティのトレイルを一時的に出す
//   - JustEvadeSuccessEvent を受け、VFX_Dash を消して VFX_JustEvade に差し替える
//
// 備考:
//   イベントを受けるだけにして、移動の System(PlayerMove/PlayerDash) が VFX を知らないようにする
//   （HitEffectSystem と同じ形）。見た目はプレハブ Assets/Prefabs/Particles/VFX_Jump・VFX_Dash で
//   エディタから調整し、コードはどこに出すかだけを決める
//   VFX_GroundJump・VFX_JustEvade は仮置き（VFX_Jump・VFX_Dash の複製。JustEvade は色だけ水色）
//   再生は VFX::Play に任せる。生成・向きの確定・バーストの発生・粒子が消えた後の破棄は
//   VFXSystem が行い、この System は VFX のエンティティを管理しない（全エミッタが Burst である前提）
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"

class PlayerActionVfxSystem : public CCL::ECS::IfSystem<
    PlayerActionVfxSystem>
{
public:
    PlayerActionVfxSystem() : IfSystem("PlayerActionVfxSystem") {}

    void Initialize() override;     // PlayerJumpedEvent / PlayerDashedEvent / JustEvadeSuccessEvent を購読する
    void Update(float dt) override {}; // 毎フレームの処理はない（再生と寿命は VFXSystem が持つ）

private:
    // ダッシュの VFX をすぐに消し、成立した瞬間のプレイヤーの位置に VFX_JustEvade を出す
    void ReplaceDashVfxWithJustEvade(CCL::ECS::EntityID playerID);

    // プレイヤーの直接の子のうち、TrailComponent を持つものを burstDuration 秒だけ出す
    void ActivatePlayerFootTrails(CCL::ECS::EntityID playerID);

    // 最後に出したダッシュの VFX。ジャスト回避が成立したら消す
    // 寿命で先に消えていても、ID の世代がずれるので消し間違えない
    CCL::ECS::EntityID m_dashVfxID = CCL::ECS::InvalidEntityID;
};
