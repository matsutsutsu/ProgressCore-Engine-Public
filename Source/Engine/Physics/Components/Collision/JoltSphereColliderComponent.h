#pragma once
// ===========================================================================
// 概要: 球形のコライダー。JoltRigidbodyComponent と一緒に付けると剛体の形状になる。
//
// 責務:
//   - 半径と、Entity の原点からの位置のずれを持つ（球なので回転は持たない）
// ===========================================================================
#include <DirectXMath.h>

struct JoltSphereColliderComponent
{
    float             radius      = 0.5f;               // 半径（m）
    DirectX::XMFLOAT3 localOffset = {0.0f, 0.0f, 0.0f}; // Entity の原点からの位置のずれ（m）

    bool isDirty = false; // 形状を作り直すか。インスペクタで編集すると自動で立つ
};
