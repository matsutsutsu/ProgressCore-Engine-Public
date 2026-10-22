#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュの設定値と状態を持つ Component。
//
// 責務:
//   - ダッシュの速度・時間・クールダウンと、ジャスト回避の受付時間を持つ
//   - 回避中の無敵時間を持つ
//
// 備考: 状態を書くのは PlayerDashSystem。
//       ジャスト回避成立後の「ラッシュ」は PlayerRushComponent（別物）。
// ===========================================================================
#include <SimpleMath.h>

struct PlayerDashComponent
{
    // --- 設定値 ---
    float dashSpeed        = 40.0f; // ダッシュ速度（m/秒）
    float dashDuration     = 0.15f; // ダッシュ時間（秒）
    float dashCooldown     = 0.5f;  // 次に出せるまでの待ち時間（秒）
    float justEvadeWindow  = 0.08f; // 開始からジャスト回避を受け付ける長さ（秒。短いほど難しい）
    float invincibleWindow = 0.06f; // 回避終了後も無敵が残る長さ（秒）

    // --- 実行時の状態 ---
    bool isDashing               = false; // 今ダッシュ中か
    bool isInvincible            = false; // 無敵中か（被弾処理が見る）
    bool isJustEvadeWindowActive = false; // ジャスト回避の受付窓が開いているか

    float                        currentTimer  = 0.0f;      // ダッシュ中=残り時間 / ダッシュ外=クールダウン残り（秒）
    DirectX::SimpleMath::Vector3 dashDirection = {0, 0, 0}; // ダッシュの移動方向（正規化済み）
};
