#pragma once
// ===========================================================================
// 概要: ビュー行列・プロジェクション行列と視錐台を持つ、描画用のカメラ
//
// 責務:
//   - 視点・注視点・上方向からビュー行列を作り、前・右・上の向きを求める
//   - 画角・アスペクト比・クリップ距離から透視投影の行列を作る
//   - 行列が変わるたびに、ワールド空間の視錐台を作り直す
//
// 備考: 実体は Graphics が1つだけ持ち（m_mainCamera）、CameraBrainSystem が毎フレーム書き込む
// ===========================================================================
#include <DirectXMath.h>
#include <DirectXCollision.h>

class Camera
{
public:
    Camera();

    // 視点 eye から注視点 focus を向くビュー行列を作る
    void SetLookAt(const DirectX::XMFLOAT3 &eye, const DirectX::XMFLOAT3 &focus, const DirectX::XMFLOAT3 &up);

    // 透視投影の行列を作る。fovY は縦の画角（ラジアン）、aspect は幅 / 高さ
    void SetPerspectiveFov(float fovY, float aspect, float nearZ, float farZ);

    // 正射影の行列を作る（幅と高さは m）。画角がないので、GetFovY は 0 を返す
    void SetOrthographic(float width, float height, float nearZ, float farZ);

    const DirectX::XMFLOAT4X4 &GetView() const { return view; }
    const DirectX::XMFLOAT4X4 &GetProjection() const { return projection; }

    const DirectX::XMFLOAT3 &GetEye() const { return eye; }
    const DirectX::XMFLOAT3 &GetFocus() const { return focus; }
    const DirectX::XMFLOAT3 &GetUp() const { return up; }
    const DirectX::XMFLOAT3 &GetFront() const { return front; }
    const DirectX::XMFLOAT3 &GetRight() const { return right; }

    // ビュー行列 × プロジェクション行列
    DirectX::XMFLOAT4X4 GetViewProjectionMatrix() const
    {
        using namespace DirectX;
        XMMATRIX V  = XMLoadFloat4x4(&view);
        XMMATRIX P  = XMLoadFloat4x4(&projection);
        XMMATRIX VP = XMMatrixMultiply(V, P);

        XMFLOAT4X4 result;
        XMStoreFloat4x4(&result, VP);
        return result;
    }

    // ワールド空間の視錐台（行列を変えるたびに作り直している）
    const DirectX::BoundingFrustum &GetFrustum() const { return frustum; }

    const float &GetFovY() const { return fovY; }
    const float &GetAspect() const { return aspect; }
    const float &GetNear() const { return nearZ; }
    const float &GetFar() const { return farZ; }

private:
    // プロジェクション行列から視錐台を作り、ビューの逆行列でワールド空間へ移す
    void UpdateFrustum();

    // コンストラクタは SetPerspectiveFov → UpdateFrustum の順に走り、UpdateFrustum は
    // view の逆行列を取る。既定値がないと、その時点で未初期化の値を読むので単位行列を入れておく
    DirectX::XMFLOAT4X4 view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    DirectX::XMFLOAT4X4 projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    DirectX::XMFLOAT3 eye{0.0f, 0.0f, 0.0f};   // 視点（ワールド, m）
    DirectX::XMFLOAT3 focus{0.0f, 0.0f, 0.0f}; // 注視点（ワールド, m）

    DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};    // カメラの上方向（ビュー行列から求めた単位ベクトル）
    DirectX::XMFLOAT3 front{0.0f, 0.0f, 1.0f}; // カメラの前方向
    DirectX::XMFLOAT3 right{1.0f, 0.0f, 0.0f}; // カメラの右方向

    float fovY   = 0.0f;    // 縦の画角（rad）
    float aspect = 1.0f;    // アスペクト比（幅 / 高さ）
    float nearZ  = 0.1f;    // 近クリップ面までの距離（m）
    float farZ   = 1000.0f; // 遠クリップ面までの距離（m）

    DirectX::BoundingFrustum frustum; // ワールド空間の視錐台
};
