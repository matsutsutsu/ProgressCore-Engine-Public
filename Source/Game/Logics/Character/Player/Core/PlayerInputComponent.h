#pragma once

// ボタンを「このステップで押した瞬間か」まで判定した結果。書くのは PlayerInputSystem だけ
// 移動の向きはここに持たず、AI と同じく CharacterMovementComponent.desiredVelocity へ書く
struct PlayerInputComponent
{
    // --- このステップの入力（PlayerInputSystem が毎ステップ書く）---

    bool attackPressed = false; // 攻撃を押した瞬間
    bool jumpPressed   = false; // ジャンプを押した瞬間
    bool dashPressed   = false; // 回避ダッシュを押した瞬間
    bool lockOnPressed = false; // ロックオンの切り替えを押した瞬間

    // --- 前のステップの押下状態（PlayerInputSystem 専用）---

    // 入力を止めている間も、実際の押下状態を書き続ける
    bool prevAttack = false;
    bool prevJump   = false;
    bool prevDash   = false;
    bool prevLockOn = false;
};
