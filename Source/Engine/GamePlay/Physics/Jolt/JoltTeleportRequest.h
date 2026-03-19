#pragma once
#include <DirectXMath.h>

// 物理オブジェクトを「強制ワープ」させたい時だけ付与するコンポーネント（1フレームで消滅する）
struct JoltTeleportRequest {
  DirectX::XMFLOAT3 targetPosition;
  // 必要であれば rotation も追加できます
};