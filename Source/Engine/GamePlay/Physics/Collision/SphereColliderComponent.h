#pragma once
#include "ECS/Common/CCL_Common.h"
#include "CollisionLayers.h"
#include <DirectXMath.h>

// 球体衝突判定用コンポーネント
struct SphereColliderComponent {
    float radius = 1.0f;       // 球の半径
    bool isColliding = false;  // 衝突中かどうかのフラグ

    // デバッグや後続処理で「何と当たったか」を知る
     CCL::ECS::EntityID lastHitEntity = CCL::ECS::InvalidEntityID;

     //  衝突情報（反射用）
     DirectX::XMFLOAT3 lastHitPos    = {0, 0, 0};
     DirectX::XMFLOAT3 lastHitNormal = { 0, 0, 0 };

     // フィルタリング用データ
     Layer layer = Layer::Default; // 自分
     Layer mask  = Layer::All;
};