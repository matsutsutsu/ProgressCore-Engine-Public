#pragma once
#include <DirectXMath.h>

// 箱形のコライダー。JoltRigidbodyComponent と一緒に付けると剛体の形状になる
// 生成は JoltBodySetupSystem、実行中の形状変更は JoltBoxShapeUpdateSystem が行う
struct JoltBoxColliderComponent
{
    DirectX::XMFLOAT3 halfExtent         = {0.5f, 0.5f, 0.5f}; // 各軸の半分の大きさ（m）。Jolt の BoxShape に合わせる
    DirectX::XMFLOAT3 localOffset        = {0.0f, 0.0f, 0.0f}; // Entity の原点からの位置のずれ（m）
    DirectX::XMFLOAT3 localRotationEuler = {0.0f, 0.0f, 0.0f}; // コライダー単体のローカル回転（度）

    bool isDirty = false; // 形状を作り直すか。インスペクタで編集すると自動で立つ
};
