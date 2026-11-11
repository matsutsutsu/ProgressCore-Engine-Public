#pragma once
// ===========================================================================
// 概要: 画面の2D座標から3D空間への変換を行うユーティリティ ScreenToWorld
//
// 責務:
//   - マウス座標から Y=0 平面との交点を求める
//   - マウス座標からピッキング用のレイ（始点 + 方向）を作る
// ===========================================================================
#include <DirectXMath.h>
#include "SimpleMath.h"
#include "Engine/Graphics/SceneView/Camera.h"

class ScreenToWorld
{
public:
    // マウスの画面座標から、ワールドの Y=0 の平面との交点 (X, 0, Z) を求める。レイが平面と平行なら (0, 0, 0)
    static DirectX::SimpleMath::Vector3 GetRayIntersectionY0(
        float mouseX, float mouseY,
        float screenWidth, float screenHeight,
        const Camera &camera);

    // 画面座標から、ピッキング用のレイ（始点はニアクリップ面上、向きは正規化）を作る。物を選ぶときに使う
    // マウスと画面の大きさが同じ座標系なら単位は問わないので、GameView は画像の中の座標と表示の大きさを渡せばよい
    static void GetRay(
        float mouseX, float mouseY,
        float screenWidth, float screenHeight,
        const Camera                 &camera,
        DirectX::SimpleMath::Vector3 &outOrigin,
        DirectX::SimpleMath::Vector3 &outDir);
};
