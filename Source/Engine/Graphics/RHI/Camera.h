#pragma once

#include <DirectXMath.h>
#include <DirectXCollision.h>

// カメラ
class Camera
{
public:
    Camera();

    // 指定方向を向く
    void SetLookAt(const DirectX::XMFLOAT3 &eye, const DirectX::XMFLOAT3 &focus, const DirectX::XMFLOAT3 &up);

    // パースペクティブ設定
    void SetPerspectiveFov(float fovY, float aspect, float nearZ, float farZ);

    // ビュー行列取得
    const DirectX::XMFLOAT4X4 &GetView() const { return view; }

    // プロジェクション行列取得
    const DirectX::XMFLOAT4X4 &GetProjection() const { return projection; }

    // 視点取得
    const DirectX::XMFLOAT3 &GetEye() const { return eye; }

    // 注視点取得
    const DirectX::XMFLOAT3 &GetFocus() const { return focus; }

    // 上方向取得
    const DirectX::XMFLOAT3 &GetUp() const { return up; }

    // 前方向取得
    const DirectX::XMFLOAT3 &GetFront() const { return front; }

    // 右方向取得
    const DirectX::XMFLOAT3 &GetRight() const { return right; }

    // ビュー・プロジェクション行列をまとめて取得
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

    // 正射影設定(シャドウマップに必要)
    void SetOrthographic(float width, float height, float nearZ, float farZ);

    // 最新の視錐台を取得する
    const DirectX::BoundingFrustum &GetFrustum() const { return frustum; }

    // 視野角取得
    const float &GetFovY() const { return fovY; }

    // アスペクト比取得
    const float &GetAspect() const { return aspect; }

    // 近クリップ面までの距離を取得
    const float &GetNear() const { return nearZ; }

    // 遠クリップ面までの距離を取得
    const float &GetFar() const { return farZ; }

private:
    // カメラの行列が変化したときにFrustumを再計算する内部関数
    void UpdateFrustum();

    // コンストラクタは SetPerspectiveFov -> UpdateFrustum の順に走り、UpdateFrustum は
    //   view の逆行列を取る。既定値が無いと、その時点で未初期化の float を読むことになる
    //  （直後の SetLookAt が上書きするので実害は出ていなかったが、単位行列を入れておく）。
    DirectX::XMFLOAT4X4 view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    DirectX::XMFLOAT4X4 projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    DirectX::XMFLOAT3 eye{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 focus{0.0f, 0.0f, 0.0f};

    DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 front{0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 right{1.0f, 0.0f, 0.0f};

    float fovY   = 0.0f;
    float aspect = 1.0f;
    float nearZ  = 0.1f;
    float farZ   = 1000.0f;

    // カメラ自身の視界
    DirectX::BoundingFrustum frustum;
};
