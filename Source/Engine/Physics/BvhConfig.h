#pragma once
#include <DirectXMath.h>

// tinybvh の型定義を使えるようにする
// 実装(TINYBVH_IMPLEMENTATION)はここには書かず、cpp側で1回だけ書く
#include "tiny_bvh.h"

namespace MyBvh {
// DirectX::XMFLOAT3 -> tinybvh::bvhvec3 変換
inline tinybvh::bvhvec3 ToVec3(const DirectX::XMFLOAT3 &v) {
    return tinybvh::bvhvec3(v.x, v.y, v.z);
}

// tinybvh::bvhvec3 -> DirectX::XMFLOAT3 変換
inline DirectX::XMFLOAT3 ToFloat3(const tinybvh::bvhvec3 &v) {
    return DirectX::XMFLOAT3(v.x, v.y, v.z);
}
} // namespace MyBvh