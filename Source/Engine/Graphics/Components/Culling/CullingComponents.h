#pragma once
// フラスタムカリングで使う境界箱（BoundingBoxComponent）と表示フラグ（VisibilityComponent）。保存しない
// CullingSetupSystem が付け、BoundsUpdateSystem が AABB を、FrustumCullingSystem が isVisible を書く
#include <DirectXCollision.h>

struct BoundingBoxComponent
{
    DirectX::BoundingBox worldAABB; // ワールド空間の軸平行境界箱（m）
};

struct VisibilityComponent
{
    bool isVisible = true; // カメラの視錐台に入っているか。FrustumCullingSystem が毎フレーム上書きする
};
