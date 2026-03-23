#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltBoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltSphereColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltCapsuleColliderComponent.h"

// ===================================================================================
// 物理コライダーの大きさを、自前の ShapeRenderer を使って可視化するシステム群。
// SystemManager のインスペクタで「Debug」にチェックを入れると描画されます。
// ===================================================================================

// 1. BoxCollider 可視化システム
class JoltBoxDebugDrawSystem : public CCL::ECS::IfSystem<JoltBoxDebugDrawSystem,
                                     CCL::ECS::Read<TransformComponent>,
                                     CCL::ECS::Read<JoltBoxColliderComponent>> {
public:
    JoltBoxDebugDrawSystem() : IfSystem("JoltBoxDebugDrawSystem") {
        // システム生成時に最初からデバッグ描画をONにする
        this->isDebugVisible = true;
    }
    
    // ロジック（計算）は何もしない
    void Update(float dt) override {} 
    
    // デバッグ描画フェーズで呼ばれる
    void OnDrawDebug(ShapeRenderer* renderer) override;
};

// 2. SphereCollider 可視化システム
class JoltSphereDebugDrawSystem : public CCL::ECS::IfSystem<JoltSphereDebugDrawSystem,
                                        CCL::ECS::Read<TransformComponent>,
                                        CCL::ECS::Read<JoltSphereColliderComponent>> {
public:
    JoltSphereDebugDrawSystem() : IfSystem("JoltSphereDebugDrawSystem") {
        // システム生成時に最初からデバッグ描画をONにする
        this->isDebugVisible = true;
    }
    void Update(float dt) override {}
    void OnDrawDebug(ShapeRenderer* renderer) override;
};

// 3. CapsuleCollider 可視化システム
class JoltCapsuleDebugDrawSystem : public CCL::ECS::IfSystem<JoltCapsuleDebugDrawSystem,
                                         CCL::ECS::Read<TransformComponent>,
                                         CCL::ECS::Read<JoltCapsuleColliderComponent>> {
public:
    JoltCapsuleDebugDrawSystem() : IfSystem("JoltCapsuleDebugDrawSystem") {
        // システム生成時に最初からデバッグ描画をONにする
        this->isDebugVisible = true;
    }
    void Update(float dt) override {}
    void OnDrawDebug(ShapeRenderer* renderer) override;
};