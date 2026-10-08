#pragma once
// ===========================================================================
// 概要: フラスタムカリングで使う、境界箱と表示フラグのコンポーネント。
//
// 責務:
//   - BoundingBoxComponent: ワールド空間の AABB を持つ
//   - VisibilityComponent: 今フレーム描くかどうかを持つ
//
// 備考: どちらも実行時に計算する値なので、シーンには保存しない。
//       CullingSetupSystem が付け、BoundsUpdateSystem が AABB を更新し、
//       FrustumCullingSystem が isVisible を毎フレーム書き換える。
// ===========================================================================
#include <DirectXCollision.h>

struct BoundingBoxComponent
{
    DirectX::BoundingBox worldAABB; // ワールド空間の軸平行境界箱（m）
};

struct VisibilityComponent
{
    bool isVisible = true; // カメラの視錐台に入っているか。FrustumCullingSystem が毎フレーム上書きする
};
