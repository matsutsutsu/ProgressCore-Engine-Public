#pragma once
// ===========================================================================
// 概要: 実行中にコライダーが変更（isDirty）されたら、剛体の形状を作り直す System 群。
//
// 責務:
//   - Box / Sphere / Capsule ごとに、isDirty の立った剛体へ新しい形状を SetShape する
//
// 備考: 形状ごとに System を分けて、ループ内で GetComponent をしないようにしている。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h" // キャラ除外用

// BoxCollider
class JoltBoxShapeUpdateSystem : public CCL::ECS::IfSystem<JoltBoxShapeUpdateSystem,
                                                           CCL::ECS::Read<JoltHandleComponent>,
                                                           CCL::ECS::Write<JoltBoxColliderComponent>>
{
public:
    JoltBoxShapeUpdateSystem() : IfSystem("JoltBoxShapeUpdateSystem") {}

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // JoltBodySetup が生成した bodyID に対して SetShape する。生成が後に回るとその1フレームは形状更新が空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltBodySetupSystem"}; }

    void Update(float dt) override;
};

// SphereCollider
class JoltSphereShapeUpdateSystem : public CCL::ECS::IfSystem<JoltSphereShapeUpdateSystem,
                                                              CCL::ECS::Read<JoltHandleComponent>,
                                                              CCL::ECS::Write<JoltSphereColliderComponent>>
{
public:
    JoltSphereShapeUpdateSystem() : IfSystem("JoltSphereShapeUpdateSystem") {}

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // JoltBodySetup が生成した bodyID に対して SetShape する。生成が後に回るとその1フレームは形状更新が空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltBodySetupSystem"}; }

    void Update(float dt) override;
};

// CapsuleCollider
class JoltCapsuleShapeUpdateSystem : public CCL::ECS::IfSystem<JoltCapsuleShapeUpdateSystem,
                                                               CCL::ECS::Read<JoltHandleComponent>,
                                                               CCL::ECS::Write<JoltCapsuleColliderComponent>>
{
public:
    JoltCapsuleShapeUpdateSystem() : IfSystem("JoltCapsuleShapeUpdateSystem")
    {
        // キャラの presence プロキシは JoltCharacterSetupSystem が足元オフセット付きで作る。
        //   この System は capsule.localOffset を使うため、キャラに適用すると
        //   CharacterVirtual と形状がずれる。そのためキャラは除外する。
        SetExclude<JoltCharacterHandleComponent>();
    }

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // JoltBodySetup が生成した bodyID に対して SetShape する。生成が後に回るとその1フレームは形状更新が空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltBodySetupSystem"}; }

    void Update(float dt) override;
};