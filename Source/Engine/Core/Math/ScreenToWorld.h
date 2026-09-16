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
        const Camera &camera);

    /**
     * @brief スクリーン座標から、ワールド空間のピッキング用レイ(始点+方向)を生成する。
     *
     * Y=0 平面に限定しない汎用レイ。オブジェクト選択(レイ-AABB判定)に使う。
     * mouseX/mouseY と screenWidth/screenHeight は「同じ座標系」であれば単位は不問。
     * GameView のように画像内相対座標(local)と表示サイズ(imageSize)を渡せば、
     * レンダーターゲットの実解像度やウィンドウ位置に依存せず正しいレイになる。
     *
     * @param mouseX,mouseY      対象領域の左上を原点としたマウス座標
     * @param screenWidth/Height 対象領域のサイズ(mouseと同じ座標系)
     * @param camera             現在の描画カメラ
     * @param outOrigin          [out] レイ始点(ニアクリップ上の点)
     * @param outDir             [out] 正規化されたレイ方向
     */
    static void GetRay(
        float mouseX, float mouseY,
        float screenWidth, float screenHeight,
        const Camera                 &camera,
        DirectX::SimpleMath::Vector3 &outOrigin,
        DirectX::SimpleMath::Vector3 &outDir);
};
