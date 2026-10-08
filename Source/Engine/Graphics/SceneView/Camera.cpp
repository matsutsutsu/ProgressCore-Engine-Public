#include "Engine/Graphics/SceneView/Camera.h"

Camera::Camera()
{
    SetPerspectiveFov(
        DirectX::XMConvertToRadians(45), // 画角
        1280.0f / 720.0f,                // アスペクト比
        0.1f,                            // 近クリップ
        1000.0f                          // 遠クリップ
    );
    SetLookAt(
        {0, 0, -5}, // 視点
        {0, 0, 0},  // 注視点
        {0, 1, 0}   // 上方向
    );
}

void Camera::SetLookAt(const DirectX::XMFLOAT3 &eye, const DirectX::XMFLOAT3 &focus, const DirectX::XMFLOAT3 &up)
{
    DirectX::XMVECTOR Eye   = DirectX::XMLoadFloat3(&eye);
    DirectX::XMVECTOR Focus = DirectX::XMLoadFloat3(&focus);
    DirectX::XMVECTOR Up    = DirectX::XMLoadFloat3(&up);
    DirectX::XMMATRIX View  = DirectX::XMMatrixLookAtLH(Eye, Focus, Up);
    DirectX::XMStoreFloat4x4(&view, View);

    // ビュー行列の逆行列はカメラのワールド行列。その各行が右・上・前の向きになる
    DirectX::XMMATRIX   World = DirectX::XMMatrixInverse(nullptr, View);
    DirectX::XMFLOAT4X4 world;
    DirectX::XMStoreFloat4x4(&world, World);

    this->right.x = world._11;
    this->right.y = world._12;
    this->right.z = world._13;

    this->up.x = world._21;
    this->up.y = world._22;
    this->up.z = world._23;

    this->front.x = world._31;
    this->front.y = world._32;
    this->front.z = world._33;

    this->eye   = eye;
    this->focus = focus;

    UpdateFrustum();
}

void Camera::SetPerspectiveFov(float fovY, float aspect, float nearZ, float farZ)
{
    DirectX::XMMATRIX Projection = DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ);
    DirectX::XMStoreFloat4x4(&projection, Projection);

    // GetAspect() などが返せるように、作るのに使った値も持っておく
    this->fovY   = fovY;
    this->aspect = aspect;
    this->nearZ  = nearZ;
    this->farZ   = farZ;

    UpdateFrustum();
}

void Camera::SetOrthographic(float width, float height, float nearZ, float farZ)
{
    DirectX::XMMATRIX Ortho = DirectX::XMMatrixOrthographicLH(width, height, nearZ, farZ);
    DirectX::XMStoreFloat4x4(&projection, Ortho);

    this->fovY  = 0.0f;
    this->nearZ = nearZ;
    this->farZ  = farZ;
    if (height != 0.0f) this->aspect = width / height;

    UpdateFrustum();
}

void Camera::UpdateFrustum()
{
    // 1. プロジェクション行列から、原点にあるカメラの視錐台（ビュー空間）を作る
    DirectX::BoundingFrustum frustumLocal;
    DirectX::BoundingFrustum::CreateFromMatrix(frustumLocal, DirectX::XMLoadFloat4x4(&projection));

    // 2. ビュー行列の逆行列（カメラのワールド行列）を求める
    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&view));

    // 3. 視錐台をカメラの位置と向きに合わせてワールド空間へ移す
    frustumLocal.Transform(frustum, invView);
}
