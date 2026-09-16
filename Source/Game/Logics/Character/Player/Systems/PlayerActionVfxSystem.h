#pragma once
// プレイヤーのアクション（2段ジャンプ・ダッシュ）に合わせて、その場にVFXを出す演出システム。
//
// 設計方針:
//   - PlayerJumpedEvent / PlayerDashedEvent(EventBus) を購読するだけのトリガ受け。
//     移動系システム(PlayerMove/PlayerDash)とVFXを疎結合にする（HitEffectSystem と同じ流儀）。
//   - 見た目はプレハブ Assets/Prefabs/Particles/VFX_Jump・VFX_Dash に集約。
//     デザイナーがエディタで調整でき、コードは「どこに出すか」だけを担う（表現と供給の分離）。
//   - 再生は VFX::Play の1行に委ねる。生成・姿勢確定・バースト発火・粒子消滅後の破棄は
//     すべて VFXSystem の責務であり、本システムは実体を管理しない（全エミッタ Burst 前提）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_Types.h"

class PlayerActionVfxSystem : public CCL::ECS::IfSystem<PlayerActionVfxSystem>
{
public:
    PlayerActionVfxSystem()
        : IfSystem("PlayerActionVfxSystem")
    {
    }

    void Initialize() override;     // PlayerJumpedEvent / PlayerDashedEvent を購読
    void Update(float dt) override; // 毎フレームの仕事は無い（再生と寿命は VFXSystem 側）

private:
    void ActivePlayerFootTrails(CCL::ECS::EntityID playerID);
};
