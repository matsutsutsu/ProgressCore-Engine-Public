#pragma once
// ===========================================================================
// 概要: JoltCharacterConfigComponent を持つ Entity に CharacterVirtual と presence プロキシを生成する System。
//
// 責務:
//   - カプセルコライダーから、足元を原点にしたキャラクター形状を作る
//   - CharacterVirtual を生成して JoltCharacterHandleComponent を付ける
//   - 他者（レイ・攻撃判定）から見える Kinematic のプロキシ剛体を生成して JoltHandleComponent を付ける
//
// 備考: JoltCapsuleColliderComponent が無い Entity は作らない。
//       生成済み（JoltCharacterHandleComponent あり）の Entity は SetExclude で対象外。
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