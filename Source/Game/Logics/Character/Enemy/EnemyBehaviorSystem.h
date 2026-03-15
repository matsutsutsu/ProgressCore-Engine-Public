#pragma once
#include "ECS/System/CCL_System.h"
#include "EnemyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include <SimpleMath.h>

struct Grid;
struct StageMap;
class ShapeRenderer;
struct CombatDirector;

// ===================================================================================
// ファイル名: EnemyBehaviorSystem.h
// クラス名: EnemyBehaviorSystem
//
// 【役割・責任】
//   ・敵キャラクターのAIロジック（思考・移動・攻撃）を実行するECSシステム
//   ・「状況判断（FSM）」と「自律移動（Steering）」を統合し、賢い敵の動きを実現する
//
// 【仕様】
//   ・処理対象: EnemyComponent, Transform, RigidBody を持つ全てのエンティティ
//
//   ・主な機能:
//     1. ステート更新: プレイヤーとの距離やタイマーに基づいて状態（Idle/Chase/Attack）を切り替える
//     2. 統合移動計算 (ComputeSteering):
//        - 「目的地への誘導(Seek)」
//        - 「仲間との分離(Separation)」
//        - 「壁回避(Wall Avoidance)」
//        これら3つの力を合成し、最適な移動ベクトルを算出する
//     3. デバッグ機能:
//        - OnDrawDebug: 現在のステートや視界範囲を可視化
//        - OnGui: ImGuiによるリアルタイムパラメータ調整
// ===================================================================================


class EnemyBehaviorSystem : public CCL::ECS::IfSystem<EnemyBehaviorSystem,
                                CCL::ECS::Write<EnemyComponent>,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<RigidBodyComponent>> {
public:
    using Vector3 = DirectX::SimpleMath::Vector3;

    EnemyBehaviorSystem() : IfSystem("EnemyBehaviorSystem") {}

    void Update(float dt) override;
    void OnGui() override;

    void OnDrawDebug(ShapeRenderer *renderer) override;

private:
    // --- ステート別更新関数 (リファクタリングで追加) ---
    void UpdateIdleState(EnemyComponent& enemy, RigidBodyComponent& rb, bool isPlayerFound, float distToPlayer);
    
    void UpdateChaseState(
        CCL::ECS::EntityID id, float dt, 
        EnemyComponent& enemy, TransformComponent& trans, RigidBodyComponent& rb,
        const Vector3& playerPos, float distToPlayer, const Vector3& dirToPlayer, const Vector3& playerForward,
        const Grid* grid, const StageMap* map,CombatDirector& director
    );

    void UpdateChargeState(float dt,
        EnemyComponent          &enemy,
        TransformComponent      &trans,
        RigidBodyComponent      &rb,
        const Vector3           &dirToPlayer,
        CombatDirector          &director);
    void UpdateJumpState(EnemyComponent &enemy, RigidBodyComponent &rb, CombatDirector &director);
    void UpdateDodgeState(EnemyComponent& enemy);
    void UpdateRangeAttackState(float dt, EnemyComponent& enemy, TransformComponent& trans, const Vector3& dirToPlayer);
    void UpdateAttackState(CCL::ECS::EntityID id, // 追加
        float                       dt,
        EnemyComponent             &enemy,
        TransformComponent         &trans,
        RigidBodyComponent         &rb,
        CCL::ECS::EntityID          playerID,
        const Vector3              &playerPos,   // 追加
        const Vector3              &dirToPlayer, // そのまま利用
        CombatDirector             &director);


    // 統合された移動計算ロジック
    Vector3 ComputeSteering(
        CCL::ECS::EntityID id, 
        const Vector3& position, 
        const Vector3& velocity,
        const Vector3& targetPos, 
        const EnemyComponent& enemy,
        const Grid* grid, 
        const StageMap* map
    );

    // 下位ヘルパー
    Vector3 ComputeWallAvoidance(const Vector3& position, const Vector3& velocity, const StageMap& map);
    Vector3 ComputeSeparation(CCL::ECS::EntityID selfID, const Vector3& position, const Grid& grid);

    // 敵の弾を発射するヘルパー関数
    void SpawnEnemyBullet(const Vector3 &position, const Vector3 &direction);

};