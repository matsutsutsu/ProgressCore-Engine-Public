#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h" // キャラ除外用

// ===================================================================================
// 実行時にコライダーの形状変更(isDirty)を検知し、Joltの剛体に「外科手術」を行うシステム群。
// パフォーマンスを最大化するため、GetComponentを排除し、形状ごとにシステムを分割しています。
// ===================================================================================

// 1. BoxCollider 専用更新システム
class JoltBoxShapeUpdateSystem : public CCL::ECS::IfSystem<JoltBoxShapeUpdateSystem,
                                     CCL::ECS::Read<JoltHandleComponent>,
                                     CCL::ECS::Write<JoltBoxColliderComponent>> {
public:
    JoltBoxShapeUpdateSystem() : IfSystem("JoltBoxShapeUpdateSystem") {}

    /// @brief 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    ///        JoltBodySetup が生成した bodyID に対して SetShape する。生成が後に回るとその1フレームは形状更新が空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltBodySetupSystem"}; }

    void Update(float dt) override;
};

// 2. SphereCollider 専用更新システム
class JoltSphereShapeUpdateSystem : public CCL::ECS::IfSystem<JoltSphereShapeUpdateSystem,
                                        CCL::ECS::Read<JoltHandleComponent>,
                                        CCL::ECS::Write<JoltSphereColliderComponent>> {
public:
    JoltSphereShapeUpdateSystem() : IfSystem("JoltSphereShapeUpdateSystem") {}

    /// @brief 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    ///        JoltBodySetup が生成した bodyID に対して SetShape する。生成が後に回るとその1フレームは形状更新が空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltBodySetupSystem"}; }

    void Update(float dt) override;
};

// 3. CapsuleCollider 専用更新システム
class JoltCapsuleShapeUpdateSystem : public CCL::ECS::IfSystem<JoltCapsuleShapeUpdateSystem,
                                         CCL::ECS::Read<JoltHandleComponent>,
                                         CCL::ECS::Write<JoltCapsuleColliderComponent>> {
public:
    JoltCapsuleShapeUpdateSystem() : IfSystem("JoltCapsuleShapeUpdateSystem") {
        // キャラの presence プロキシは CharacterSetup が「足元オフセット付き」で作る。
        //   このシステムは capsule.localOffset を使うため、キャラに適用すると
        //   CharacterVirtual と形状がズレる。よってキャラは除外する。
        SetExclude<JoltCharacterHandleComponent>();
    }

    /// @brief 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    ///        JoltBodySetup が生成した bodyID に対して SetShape する。生成が後に回るとその1フレームは形状更新が空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltBodySetupSystem"}; }

    void Update(float dt) override;
};