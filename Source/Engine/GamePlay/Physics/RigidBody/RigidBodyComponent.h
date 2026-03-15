#pragma once

#include <DirectXMath.h>

struct RigidBodyComponent {
    DirectX::XMFLOAT3 velocity   = {0, 0, 0}; // 1秒間に動く量
    float             mass       = 1.0f;      // 質量（重いほど動きにくい）
    float             bounciness = 0.7f;      // 反発係数 (0.0:跳ねない, 1.0:完全に跳ねる)

    // ★修正: 役割を分ける
    // 1. 空気抵抗・減衰率 (1.0 = 減速なし, 0.98 = 徐々に遅くなる)
    // PhysicsIntegrationSystemで使用
    float linearDamping = 0.98f;

    // 2. 摩擦係数 (0.0 = ツルツル, 1.0 = ザラザラ)
    // CollisionSystemで使用
    float dynamicFriction = 0.3f;

    bool isStatic = false; // trueなら重力や衝突で動かない固定物体

    // --- 追加パラメータ ---
    bool useGravity = true;  // 重力を受けるか
    bool isGrounded = false; // 【状態】現在接地しているか（Systemが書き込む）

    float maxSpeed = 50.0f; // 個体ごとの最高速度
    float minSpeed = 5.0f;  // 個体ごとの最低速度

    // マップとの衝突判定（壁ずり）を無視するか
    // 弾丸は true に設定する
    bool ignoreMapCollision = false;
};