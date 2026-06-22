/// @file PlayerComponent.h
/// @brief プレイヤー固有のステータス（速度や設定）のみを保持するコンポーネント
#pragma once

struct PlayerComponent {

    float turnSpeed = 10.0f;

    // --- 追加：2段ジャンプ設定・状態 ---
    int  maxJumps             = 2;     ///< 最大ジャンプ可能回数 (2段なら2)
    int  currentJumpCount     = 0;     ///< 現在消費したジャンプ回数
    bool wasJumpButtonPressed = false; ///< 前フレームのボタン押下状態 (連打防止用)

};