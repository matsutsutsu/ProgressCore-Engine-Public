#pragma once
// ===========================================================================
// 概要: JoltCharacterConfigComponent を持つ Entity に、
//       CharacterVirtual と、他者から見えるプロキシ剛体を作る System。
//
// 責務:
//   - カプセルコライダーから、足元を原点にしたキャラクターの形を作る
//   - CharacterVirtual を作って JoltCharacterHandleComponent を付ける
//   - 同じ形の Kinematic のプロキシ剛体を作って JoltHandleComponent を付ける
//
// 備考: JoltCapsuleColliderComponent が無い Entity には作らない。
//       作り終えた Entity（JoltCharacterHandleComponent あり）は SetExclude で外す。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

class JoltCharacterSetupSystem : public CCL::ECS::IfSystem<JoltCharacterSetupSystem,
                                                           CCL::ECS::Read<TransformComponent>,
                                                           CCL::ECS::Read<JoltCharacterConfigComponent>>
{
public:
    JoltCharacterSetupSystem() : IfSystem("JoltCharacterSetupSystem")
    {
        SetExclude<JoltCharacterHandleComponent>();
    }
    virtual ~JoltCharacterSetupSystem() = default;

    void Update(float dt) override;
};
