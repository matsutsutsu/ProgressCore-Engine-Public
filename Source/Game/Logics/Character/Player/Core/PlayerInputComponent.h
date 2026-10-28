#pragma once
// ===========================================================================
// 概要: プレイヤーのボタンを「このステップで押した瞬間か」まで
//       判定した結果を持つ Component。
//
// 責務:
//   - 攻撃・ジャンプ・回避・ロックオンの「押した瞬間」を持つ
//   - 押した瞬間の判定に使う、前のステップの押下状態を持つ
//
// 備考:
//   書くのは PlayerInputSystem だけ。各アクションの System は読むだけ。
//   押した瞬間の判定は、PlayerInputSystem が1ステップに1回だけ行う。
//
//   移動の向きはここに持たない。
//   AI と同じく CharacterMovementComponent.desiredVelocity へ書く。
// ===========================================================================

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
