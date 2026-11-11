#pragma once
#include <SimpleMath.h>

// 被弾時の吹き飛ばしと硬直の状態。被弾させたいキャラに常に付けておく
// DamageResolutionSystem が書き、JoltCharacterUpdateSystem が水平速度へ足すので、壁を抜けない
// 被弾のたびに付け外しすると、同時に多数が被弾したときにアーキタイプの移動が起きる
struct KnockbackComponent
{
    // --- 設定（保存する）---

    float duration     = 0.18f; // 吹き飛ばす時間 [秒]。短いほど鋭く止まる
    float stunDuration = 0.35f; // 操作を受け付けない時間 [秒]。0 で硬直なし（読むのはプレイヤーだけ）

    // --- 実行時の値（保存しない）---

    DirectX::SimpleMath::Vector3 velocity  = {0.0f, 0.0f, 0.0f}; // 命中時の吹き飛ばしの速度 [m/s]
    float                        timer     = 0.0f;               // 吹き飛ばしの残り [秒]
    float                        stunTimer = 0.0f;               // 硬直の残り [秒]

    // 操作を受け付けない状態か
    bool IsStunned() const { return stunTimer > 0.0f; }
};
