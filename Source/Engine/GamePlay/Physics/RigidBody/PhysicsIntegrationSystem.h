#pragma once
#include "ECS/System/CCL_System.h" // IfSystemの継承元
#include "ECS/Core/CCL_Chunk.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Game/Logics/World/StageMap.h"

// ===================================================================================
// ファイル名: PhysicsIntegrationSystem.h
// クラス名: PhysicsIntegrationSystem
//
// 【役割・責任】
//   ・物理シミュレーションの最終工程「移動（積分）」を担当する
//   ・速度(Velocity)に基づいて座標(Position)を更新する
//   ・「壁ずり（Wall Slide）」処理を行う
//
// 【仕様】
//   ・処理順序:
//     1. 重力適用（Gravity）
//     2. 摩擦適用（Friction）
//     3. 移動量算出（Delta = Velocity * dt）
//     4. 壁判定と補正（Map Collision）
//        - X軸移動 -> 壁判定 -> 当たるならX移動キャンセル
//        - Z軸移動 -> 壁判定 -> 当たるならZ移動キャンセル
//        - これにより、壁に向かって斜めに進んだ際に「沿って滑る」挙動を実現する
//     5. 座標更新
//
//   ・依存リソース: StageMap（存在しない場合は通常の物理挙動のみ行う）
// ===================================================================================


class PhysicsIntegrationSystem : public CCL::ECS::IfSystem<
    PhysicsIntegrationSystem,
    CCL::ECS::Write<TransformComponent>, 
    CCL::ECS::Write<RigidBodyComponent>
> {
public:
	PhysicsIntegrationSystem() : IfSystem("PhysicsIntegrationSystem") {}

    void Update(float dt) override;


    private:
    // 指定座標が壁に埋まっているかチェック（4点+中心判定）
      bool CheckWallCollision(
          const StageMap &map, const DirectX::SimpleMath::Vector3 &pos, float r);
};