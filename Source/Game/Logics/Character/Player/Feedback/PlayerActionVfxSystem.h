#pragma once
// ===========================================================================
// 概要: プレイヤーの2段ジャンプとダッシュに合わせて、その場に VFX を出す System。
//
// 責務:
//   - PlayerJumpedEvent を受け、2段目のジャンプのときだけ VFX_Jump を出す
//   - PlayerDashedEvent を受け、VFX_Dash を出し、子エンティティのトレイルを一時的に出す
//
// 備考:
//   イベントを受けるだけにして、移動の System(PlayerMove/PlayerDash) が VFX を知らないようにする
//   （HitEffectSystem と同じ形）。見た目はプレハブ Assets/Prefabs/Particles/VFX_Jump・VFX_Dash で
//   エディタから調整し、コードはどこに出すかだけを決める。
//   再生は VFX::Play に任せる。生成・向きの確定・バーストの発生・粒子が消えた後の破棄は
//   VFXSystem が行い、この System は VFX のエンティティを管理しない（全エミッタが Burst である前提）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"

class PlayerActionVfxSystem : public CCL::ECS::IfSystem<PlayerActionVfxSystem>
{
public:
    PlayerActionVfxSystem() : IfSystem("PlayerActionVfxSystem") {}

    void Initialize() override;     // PlayerJumpedEvent / PlayerDashedEvent を購読する
    void Update(float dt) override; // 毎フレームの処理は無い（再生と寿命は VFXSystem が持つ）

private:
    // プレイヤーの直接の子のうち、TrailComponent を持つものを burstDuration 秒だけ出す
    void ActivatePlayerFootTrails(CCL::ECS::EntityID playerID);
};
