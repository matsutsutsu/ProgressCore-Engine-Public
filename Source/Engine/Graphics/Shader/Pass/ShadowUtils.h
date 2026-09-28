#pragma once
// ===========================================================================
// 概要: カスケードシャドウマップの、カスケードごとのライトのビュー・プロジェクション行列を求める関数。
//
// 責務:
//   - カメラの視錐台を距離で分け、それぞれを囲むライトの正射影を作る
//   - シャドウマップのテクセル単位に中心を丸め、カメラが動いたときの影のちらつきを抑える
//
// 備考: ShadowPass と ShadowMapRenderSystem が使う。
// ===========================================================================
#include <DirectXMath.h>
#include <vector>
#include <algorithm>

using namespace DirectX;

// view × proj の視錐台の8つの角を、ワールド座標で返す
inline std::vector<XMFLOAT3> GetFrustumCornersWorldSpace(const XMMATRIX &view, const XMMATRIX &proj)
{
    XMMATRIX viewProj    = view * proj;
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

    std::vector<XMFLOAT3> corners(8);
    // NDC の箱の8つの角（z は 0 が手前、1 が奥）
    const XMFLOAT3 ndcCorners[8] = {
        XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT3(1.0f, 1.0f, 0.0f),
        XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT3(-1.0f, -1.0f, 0.0f),
        XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f),
        XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(-1.0f, -1.0f, 1.0f)};

    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR v = XMLoadFloat3(&ndcCorners[i]);
        // XMVector3TransformCoord は w で割るところまで行う
        XMVECTOR transformed = XMVector3TransformCoord(v, invViewProj);
        XMStoreFloat3(&corners[i], transformed);
    }
    return corners;
}

/**
 * @brief カスケード3枚分のライトのビュー・プロジェクション行列を求める。
 * @param cameraView カメラのビュー行列
 * @param fov カメラの縦の画角（rad）
 * @param aspectRatio カメラのアスペクト比
 * @param lightDir 平行光源の向き
 * @param cascadeSplits カスケードの境目のカメラからの距離（m）。例 {15, 50, 200}
 * @param outLightVP カスケードごとの結果
 */
inline void CalculateCascadeMatrices(
    const XMMATRIX &cameraView,
    float fov, float aspectRatio,
    const XMFLOAT3 &lightDir,
    const float     cascadeSplits[3],
    XMMATRIX        outLightVP[3])
{
    // カメラからの距離で分ける境目（近クリップの 0.1m を含めて4つ）
    float frustumIntervals[4] = {0.1f, cascadeSplits[0], cascadeSplits[1], cascadeSplits[2]};

    // ライトのビュー行列（原点からライトの向きを見る。位置は正射影の範囲で決まるので原点でよい）
    XMVECTOR lDir = XMLoadFloat3(&lightDir);
    XMVECTOR up   = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    // ライトが真上・真下を向くと up と平行になり行列が作れないので、up を変える
    if (abs(XMVectorGetY(lDir)) > 0.999f) up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    XMMATRIX lightView = XMMatrixLookToLH(XMVectorZero(), lDir, up);

    for (int i = 0; i < 3; ++i)
    {
        // このカスケードの距離の範囲だけを切り出したカメラの透視投影
        XMMATRIX cascadeProj = XMMatrixPerspectiveFovLH(fov, aspectRatio, frustumIntervals[i], frustumIntervals[i + 1]);

        auto corners = GetFrustumCornersWorldSpace(cameraView, cascadeProj);

        // 8つの角をライトの空間に移し、それを囲む箱（AABB）を求める
        float minX = 100000.0f, maxX = -100000.0f;
        float minY = 100000.0f, maxY = -100000.0f;
        float minZ = 100000.0f, maxZ = -100000.0f;

        for (const auto &corner : corners)
        {
            XMVECTOR v = XMLoadFloat3(&corner);
            XMVECTOR lightSpaceV = XMVector3TransformCoord(v, lightView);

            minX = (std::min)(minX, XMVectorGetX(lightSpaceV));
            maxX = (std::max)(maxX, XMVectorGetX(lightSpaceV));
            minY = (std::min)(minY, XMVectorGetY(lightSpaceV));
            maxY = (std::max)(maxY, XMVectorGetY(lightSpaceV));
            minZ = (std::min)(minZ, XMVectorGetZ(lightSpaceV));
            maxZ = (std::max)(maxZ, XMVectorGetZ(lightSpaceV));
        }

        // 視錐台の外にある影を落とす物も描けるよう、奥行きの範囲を前後に 1000m 広げる
        minZ -= 1000.0f;
        maxZ += 1000.0f;

        // カメラを動かすと影の輪郭がちらつく（シマリング）のを抑える。
        // 範囲を長い辺に合わせた正方形にして、カメラが回っても大きさが変わらないようにし、
        // 中心をテクセルの大きさ単位に丸めて、テクセル単位でしか動かないようにする
        float frustumWidth  = maxX - minX;
        float frustumHeight = maxY - minY;
        float maxLength     = (std::max)(frustumWidth, frustumHeight);
        float halfLength    = maxLength * 0.5f;

        float centerX = (minX + maxX) * 0.5f;
        float centerY = (minY + maxY) * 0.5f;

        // シャドウマップの解像度（px）。Graphics が作る ShadowMap の大きさと合わせる
        const float SHADOW_MAP_SIZE = 2048.0f;

        float worldUnitsPerTexel = maxLength / SHADOW_MAP_SIZE; // 1テクセルの大きさ（m）

        centerX = std::floor(centerX / worldUnitsPerTexel) * worldUnitsPerTexel;
        centerY = std::floor(centerY / worldUnitsPerTexel) * worldUnitsPerTexel;

        minX = centerX - halfLength;
        maxX = centerX + halfLength;
        minY = centerY - halfLength;
        maxY = centerY + halfLength;

        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(minX, maxX, minY, maxY, minZ, maxZ);

        outLightVP[i] = lightView * lightProj;
    }
}