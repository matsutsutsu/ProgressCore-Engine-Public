//#pragma once
//#include "ECS/Core/CCL_Chunk.h"
//#include "ECS/System/CCL_System.h"
//#include "Engine/Physics/Grid.h" // 新しいGrid
//
//// 必要なコンポーネント
//#include "Engine/Physics/CollisionHelper.h"
//#include "Engine/GamePlay/Physics/Collision/BoxColliderComponent.h"
//#include "Engine/GamePlay/Physics/Collision/CapsuleColliderComponent.h"
//#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
//#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
//#include "Engine/GamePlay/Transform/TransformComponent.h"
//
//
//// ===================================================================================
//// クラス名: CollisionSystem
//// 役割: 全エンティティの衝突判定を管理するシステム
////
//// 処理の流れ:
////   1. 自身のAABB（占有範囲）を計算
////   2. Grid（空間分割）に問い合わせて、近くにいるEntityのリストを取得 (Broad Phase)
////   3. 取得したリストに対して詳細な衝突判定(Detect)を実行 (Narrow Phase)
////   4. 衝突していれば解決処理(Solve)を行い、位置と速度を更新する
//// ===================================================================================
//
//
//// 統合された衝突判定システム
//// RigidBodyを持っている（＝動く可能性がある）全てのEntityを対象にする
//class CollisionSystem : public CCL::ECS::IfSystem<CollisionSystem,
//                            CCL::ECS::Write<TransformComponent>,
//                            CCL::ECS::Write<RigidBodyComponent>> {
//  public:
//    CollisionSystem() : IfSystem("CollisionSystem") {}
//
//    void Update(float dt) override;
//    void OnDrawDebug(ShapeRenderer *renderer) override;
//
//  private:
//    // 衝突解決（Solver）
//    void SolveCollisions(const std::vector<CollisionManifold> &manifolds, int iterIndex);
//
//    // 2つのエンティティ間の詳細判定（Narrow Phase）
//    bool Detect(CCL::ECS::EntityID entityA,
//        ColliderType               typeA,
//        TransformComponent        &transA,
//        CCL::ECS::EntityID         entityB,
//        ColliderType               typeB,
//        CollisionManifold         &outManifold);
//
//    // 重複チェック用のバッファ（毎フレーム使い回す）
//    std::vector<CCL::ECS::EntityID> _neighborBuffer;
//};