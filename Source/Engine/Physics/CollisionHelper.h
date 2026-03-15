#pragma once
#include "Engine/GamePlay/Physics/Collision/BoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/CapsuleColliderComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include <SimpleMath.h>

namespace CCL::ECS::Core
{
class World;
}

// ===================================================================================
// クラス名: CollisionHelper
// 役割: 衝突判定（Detect）と衝突解決（Resolve）の数学的計算を一手に引き受けるヘルパークラス
//
// 特徴:
//   - Static関数のみで構成され、状態を持たない（純粋関数）
//   - 各形状（Sphere, Box, Capsule）ごとの交差判定アルゴリズムを実装
//   - 物理的な反射や押し出し計算もここで行う
//
// 使い方:
//   CollisionSystemから、DetectCollision() や ResolveCollision() を呼び出して使う。
// ===================================================================================

enum class ShapeType
{
    None,
    Sphere,
    Box,
    Capsule
};

// 異なるコンポーネント型を統一的に扱うためのラッパー
struct ColliderWrapper
{
    ShapeType           type          = ShapeType::None;
    void               *componentPtr  = nullptr; // 元のコンポーネントへの生ポインタ
    Layer              *layer         = nullptr; // フィルタ用
    Layer              *mask          = nullptr;
    bool               *isColliding   = nullptr; // フラグ書き込み用
    CCL::ECS::EntityID *lastHitEntity = nullptr;
    DirectX::XMFLOAT3  *lastHitNormal = nullptr;
};

// 衝突結果データ
struct CollisionManifold
{
    CCL::ECS::EntityID           entityA;
    CCL::ECS::EntityID           entityB;
    DirectX::SimpleMath::Vector3 normal;  // AからBへ向かう法線ベクトル
    float                        overlap; // めり込んでいる深さ
};

class CollisionHelper
{
  public:
    using Vector3 = DirectX::SimpleMath::Vector3;

    // EntityIDから適切なコライダを探してWrapperに包んで返す
    static ColliderWrapper GetCollider(CCL::ECS::Core::World *world, CCL::ECS::EntityID id);

    // 2つのラッパー間の衝突判定（型を見て内部で専用関数に振り分ける）
    static bool DetectCollision(const TransformComponent &transA,
        const ColliderWrapper                            &colA,
        const TransformComponent                         &transB,
        const ColliderWrapper                            &colB,
        CollisionManifold                                &outManifold);

    // 物理的な位置補正（押し出し）と速度変更（反射）を行う
    static void ResolveCollision(TransformComponent &transA,
        RigidBodyComponent                          &bodyA,
        TransformComponent                          &transB,
        RigidBodyComponent                          &bodyB,
        const Vector3                               &normal,
        float                                        overlap,
        float                                        invMassA, 
        float                                        invMassB  
    );

    // --- 各形状ペアごとの詳細判定関数 ---

// 球 vs 球
    static bool CheckSphereVsSphere(const TransformComponent &transA,
        const SphereColliderComponent                        &sphereA,
        const TransformComponent                             &transB,
        const SphereColliderComponent                        &sphereB,
        CollisionManifold                                    &outManifold);

    // 球 vs 箱
    static bool CheckSphereVsBox(const TransformComponent &transSphere,
        const SphereColliderComponent                     &sphere,
        const TransformComponent                          &transBox,
        const BoxColliderComponent                        &box,
        CollisionManifold                                 &outManifold);

    // 箱 vs 箱
    static bool CheckBoxVsBox(const TransformComponent &transA,
        const BoxColliderComponent                     &boxA,
        const TransformComponent                       &transB,
        const BoxColliderComponent                     &boxB,
        CollisionManifold                              &outManifold);

    // 球 vs カプセル
    static bool CheckSphereVsCapsule(const TransformComponent &transSphere,
        const SphereColliderComponent                         &sphere,
        const TransformComponent                              &transCapsule,
        const CapsuleColliderComponent                        &capsule,
        CollisionManifold                                     &outManifold);

    // ★追加: カプセル vs カプセル
    static bool CheckCapsuleVsCapsule(const TransformComponent &transA,
        const CapsuleColliderComponent                         &capA,
        const TransformComponent                               &transB,
        const CapsuleColliderComponent                         &capB,
        CollisionManifold                                      &outManifold);

    // ★追加: カプセル vs 箱
    static bool CheckCapsuleVsBox(const TransformComponent &transCap,
        const CapsuleColliderComponent                     &cap,
        const TransformComponent                           &transBox,
        const BoxColliderComponent                         &box,
        CollisionManifold                                  &outManifold);

  private:
    // 線分と点の最短距離計算用ヘルパー
    static float DistSqPointSegment(const DirectX::SimpleMath::Vector3 &start,
        const DirectX::SimpleMath::Vector3                             &end,
        const DirectX::SimpleMath::Vector3                             &point,
        DirectX::SimpleMath::Vector3                                   &outClosestPoint);


    // ★追加: 2つの線分間の最短距離と、それぞれの最近点を求めるヘルパー
    // (Capsule vs Capsule用)
    static float DistSegmentSegment(const Vector3 &p1,
        const Vector3                             &q1, // 線分A
        const Vector3                             &p2,
        const Vector3                             &q2, // 線分B
        Vector3                                   &outC1,
        Vector3                                   &outC2); // 結果座標


};