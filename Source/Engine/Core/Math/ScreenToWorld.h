#pragma once
#include <DirectXMath.h>
#include "SimpleMath.h"
#include "Engine/Graphics/RHI/Camera.h" // 提供されたカメラクラス


/**
 * @brief 画面の2D座標から、3D空間への変換を行う数学ユーティリティ
 */
class ScreenToWorld
{
public:
    /**
     * @brief マウスのスクリーン座標から、ワールド空間の Y=0 平面との交点を計算する
     * * @param mouseX       マウスのX座標
     * @param mouseY       マウスのY座標
     * @param screenWidth  画面の幅（ピクセル）
     * @param screenHeight 画面の高さ（ピクセル）
     * @param camera       現在の描画に使用しているカメラ
     * @return Y=0 平面上のワールド座標 (X, 0, Z)
     */
    static DirectX::SimpleMath::Vector3 GetRayIntersectionY0(
        float mouseX, float mouseY,
        float screenWidth, float screenHeight,
        const Camera& camera);
};
