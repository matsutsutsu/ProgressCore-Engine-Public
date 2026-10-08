#pragma once
// ===========================================================================
// 概要: ECS 側の Transform と設定を、物理計算の前に Jolt の剛体へ反映する System。
//
// 責務:
//   - JoltRigidbodyComponent の motionType が変わっていれば剛体へ反映する
//   - Transform の isTeleported が立っていれば剛体をその位置・回転へ移し、フラグを下ろす
//   - Kinematic の剛体は毎ステップ Transform のワールド位置・回転へ合わせる
//
// 備考: L03_PushToPhysics で動く（BoneAttachmentSystem が剣などの位置を確定させた後）。
//       キャラクター（CharacterVirtual）の isTeleported は JoltCharacterUpdateSystem が処理する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidbodyComponent.h"

class JoltPushSystem
    : public CCL::ECS::IfSystem<JoltPushSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Read<JoltHandleComponent>,
                                CCL::ECS::Read<JoltRigidbodyComponent>>
{
public:
    JoltPushSystem() : IfSystem("JoltPushSystem") {}

    void Update(float dt) override;
};