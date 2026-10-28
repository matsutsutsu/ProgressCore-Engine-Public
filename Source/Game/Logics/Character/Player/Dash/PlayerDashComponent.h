#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュの設定値と状態を持つ Component。
//       状態を書くのは PlayerDashSystem。
//
// 責務:
//   - ダッシュの速さ・長さ・クールダウンを持つ
//   - ジャスト回避の受付と、回避中の無敵の状態を持つ
//
// 備考: ジャスト回避が成立した後の「ラッシュ」は PlayerRushComponent。
// ===========================================================================
#include <SimpleMath.h>

struct PlayerDashComponent
{
    // --- 設定 ---

    float dashSpeed        = 40.0f; // ダッシュの速さ（m/秒）
    float dashDuration     = 0.15f; // ダッシュの長さ（秒）
    float dashCooldown     = 0.5f;  // 次に出せるまでの待ち（秒）
    float justEvadeWindow  = 0.08f; // 開始からジャスト回避を受け付ける長さ（秒）
    float invincibleWindow = 0.06f; // 回避が終わった後も残る無敵の長さ（秒）

    // --- 実行時の状態 ---

    bool isDashing               = false; // ダッシュ中か
    bool isInvincible            = false; // 回避の無敵を保っている最中か
    bool isJustEvadeWindowActive = false; // ジャスト回避の受付中か

    float                        currentTimer  = 0.0f;      // ダッシュ中は残り、ダッシュ外はクールダウンの残り（秒）
    DirectX::SimpleMath::Vector3 dashDirection = {0, 0, 0}; // ダッシュの向き（水平・正規化済み）
};
