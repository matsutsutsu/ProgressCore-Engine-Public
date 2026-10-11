#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュの設定値と状態を持つ Component。
//
// 責務:
//   - ダッシュの速度・時間・クールダウンと、ジャスト回避の受付時間を持つ
//   - 回避中の無敵時間を持つ
//
// 備考: 状態を書くのは PlayerDashSystem。
//       ジャスト回避成立後の「ラッシュ」（踏込/固定/集中スロー/間合い詰め）は RushComponent（別物）。
// ===========================================================================
#include <SimpleMath.h>

struct DashComponent
{
    // --- 設定値 ---
    float dashSpeed    = 40.0f; // ダッシュ速度（m/秒）
    float dashDuration = 0.15f; // ダッシュ時間（秒）
    float dashCooldown = 0.5f;  // ダッシュ終了から次のダッシュまでの待ち時間（秒）

    // ダッシュ開始からジャスト回避を受け付ける時間（秒）。短いほど難しい
    float justEvadeWindow = 0.08f;

    // 回避の無敵時間
    // isInvincible が立っている間、被弾処理が見る
    // 回避が終わっても、この秒数だけ無敵が残る。
    float invincibleWindow = 0.06f;

    // --- 実行時の状態 ---
    bool isDashing    = false;
    bool isInvincible = false;

    // ジャスト回避を受け付けている間 true
    bool isJustEvadeWindowActive = false;

    float                        currentTimer  = 0.0f;      // ダッシュ中は残り時間、ダッシュ外はクールダウンの残り（秒）
    DirectX::SimpleMath::Vector3 dashDirection = {0, 0, 0}; // ダッシュの移動方向（正規化済み）
};
