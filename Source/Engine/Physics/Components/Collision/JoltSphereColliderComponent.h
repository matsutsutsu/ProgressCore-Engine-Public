#pragma once
#include <DirectXMath.h>

// 球形のコライダー。JoltRigidbodyComponent と一緒に付けると剛体の形状になる
struct JoltSphereColliderComponent
{
    float             radius      = 0.5f;               // 半径（m）
    DirectX::XMFLOAT3 localOffset = {0.0f, 0.0f, 0.0f}; // Entity の原点からの位置のずれ（m）

    bool isDirty = false; // 形状を作り直すか。インスペクタで編集すると自動で立つ
};
