#pragma once
#include "CollisionLayers.h"
#include "ECS/Common/CCL_Common.h"
#include <DirectXMath.h>

// ===================================================================================
// クラス名: BoxColliderComponent
// 役割: 箱型（直方体）の衝突判定用データを持つコンポーネント
//
// 主要データ:
//   - center: 自身のTransform位置からのオフセット（中心ズレ）
//   - size:   幅・高さ・奥行きのサイズ（全幅）
//   - layer / mask: 「誰と当たるか」を決めるフィルタ設定
//
// 補足:
//   回転（Rotation）の情報はここには持たず、EntityのTransformComponentの回転を使用します。
//   これにより、OBB（Oriented Bounding Box: 回転する箱）として扱われます。
// ===================================================================================

struct BoxColliderComponent
{
    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f}; // オフセット
    DirectX::XMFLOAT3 size   = {1.0f, 1.0f, 1.0f}; // 幅・高さ・奥行き

    // 衝突情報記録用
    bool               isColliding   = false;
    CCL::ECS::EntityID lastHitEntity = CCL::ECS::InvalidEntityID;

    // 【修正】初期値を「Default」かつ「全員と当たる」設定に変更
    // これですり抜け事故を防ぐ
    Layer layer = Layer::Default;
    Layer mask  = Layer::All;
};