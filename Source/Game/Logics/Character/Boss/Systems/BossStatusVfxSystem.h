#pragma once
// ===========================================================================
// 概要: ボスの状態（怒り中・疲れ）の間だけ、ボスの子のパーティクルを出させる System
//
// 責務:
//   - BossStatusVfxComponent の付いたパーティクルについて、祖先のボスを探して owner に持つ
//   - ボスがその状態の間だけ emitting を立て、入った瞬間にバーストを1回起こす
//
// 備考: 怒り中は BossCommandComponent::raging、疲れは BossActionComponent::currentState を読む
//       どちらも L02 で確定するので、L06 で読めば同じフレームの値になる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Game/Logics/Character/Boss/Components/BossStatusVfxComponent.h"

class BossStatusVfxSystem : public CCL::ECS::IfSystem<
    BossStatusVfxSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<BossStatusVfxComponent>,
    CCL::ECS::Write<GPUParticleComponent>>
{
    using Base = CCL::ECS::IfSystem<
        BossStatusVfxSystem,
        CCL::ECS::Read<TransformComponent>,
        CCL::ECS::Write<BossStatusVfxComponent>,
        CCL::ECS::Write<GPUParticleComponent>>;

public:
    BossStatusVfxSystem() : IfSystem("BossStatusVfxSystem") {}

    void Update(float dt) override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    // ボスが kind の状態にいるか。ボスの部品が欠けていれば false
    bool IsInStatus(CCL::ECS::EntityID boss, BossStatusVfxKind kind) const;
};
