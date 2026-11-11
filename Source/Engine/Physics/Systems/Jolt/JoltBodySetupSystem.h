#pragma once
// ===========================================================================
// 概要: JoltRigidbodyComponent とコライダーから、Jolt 空間に剛体を生成する System
//
// 責務:
//   - Box / Sphere / Capsule / Mesh のコライダーから形状を作る
//   - JoltRigidbodyComponent の設定で剛体を生成し、UserData に EntityID を入れる
//   - 生成した Entity に JoltHandleComponent を付ける（以後は SetExclude で対象外）
//
// 備考: L03_PrePhysics で動く。形状を増やすときはここの分岐を足す
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidbodyComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"

#include <Jolt/Physics/Collision/Shape/Shape.h>

class JoltBodySetupSystem : public CCL::ECS::IfSystem<
    JoltBodySetupSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<JoltRigidbodyComponent>>
{
public:
    JoltBodySetupSystem() : IfSystem("JoltBodySetupSystem")
    {
        SetExclude<JoltHandleComponent>(); // JoltHandleComponent を持つ Entity は処理対象外
    }
    virtual ~JoltBodySetupSystem() = default;

    void Update(float dt) override;
};