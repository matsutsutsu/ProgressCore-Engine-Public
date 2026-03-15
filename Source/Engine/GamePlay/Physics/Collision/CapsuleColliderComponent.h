#pragma once
#include "CollisionLayers.h"
#include "ECS/Common/CCL_Common.h"
#include <DirectXMath.h>

struct CapsuleColliderComponent
{
    float radius = 0.5f; // 半径（胴体の太さ）
    float height = 2.0f; // 全長（頭の先から足の先まで）

    // オフセット（足元基準にするか、中心基準にするかの調整用）
    DirectX::XMFLOAT3 center = {0, 1.0f, 0};

    bool               isColliding   = false;
    CCL::ECS::EntityID lastHitEntity = CCL::ECS::InvalidEntityID;

    // 衝突情報
    DirectX::XMFLOAT3 lastHitNormal = {0, 0, 0};

    Layer layer = Layer::Default; // 初期値の例
    Layer mask  = Layer::All;
};