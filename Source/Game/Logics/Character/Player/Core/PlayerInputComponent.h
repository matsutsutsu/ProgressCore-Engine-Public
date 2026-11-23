#pragma once

// プレイヤーが入力した情報を格納するコンポーネント
// PlayerInputSystemで操作する　実際の入力の方向はCharacterMovementで行う
struct PlayerInputComponent
{
    // --- このステップの入力 ---

    bool attackPressed = false; // 攻撃を押した瞬間
    bool jumpPressed   = false; // ジャンプを押した瞬間
    bool dashPressed   = false; // 回避ダッシュを押した瞬間
    bool lockOnPressed = false; // ロックオンの切り替えを押した瞬間

    // --- 前のステップの押下状態 ---

    // 入力を止めている間も、実際の押下状態を書き続ける
    bool prevAttack = false;
    bool prevJump   = false;
    bool prevDash   = false;
    bool prevLockOn = false;
};
