#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"

// ===================================================================================
// クラス名: GridUpdateSystem
// 役割: 毎フレーム、全オブジェクトの位置をGridに登録し直すシステム
//
// 処理:
//   1. Gridをクリア
//   2. 全EntityのTransformとColliderを確認し、AABB（バウンディングボックス）を計算
//   3. Gridの該当セルにEntityIDを登録
// ===================================================================================


// ShapeRendererは前方宣言だけでOK（ポインタとしてしか使わないため）
class ShapeRenderer;

class GridUpdateSystem : public CCL::ECS::IfSystem<GridUpdateSystem,
                             CCL::ECS::Read<TransformComponent>,
                             CCL::ECS::Read<RigidBodyComponent>> {
  public:
    // コンストラクタ
    GridUpdateSystem::GridUpdateSystem()
        : IfSystem("GridUpdateSystem")
    {
    }

    // 更新処理
    void Update(float dt) override;

    // デバッグ描画
    void OnDrawDebug(ShapeRenderer* renderer) override;
};