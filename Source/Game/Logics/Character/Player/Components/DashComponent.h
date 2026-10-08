#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュと、ジャスト回避後の接近の設定値と状態を持つ Component。
//
// 責務:
//   - ダッシュの速度・時間・クールダウンと、ジャスト回避の受付時間を持つ
//   - 回避中の無敵時間を持つ
//   - ジャスト回避成立後にボスへ近づく「接近」の設定値と状態を持つ
//
// 備考: 状態を書くのは PlayerDashSystem と JustEvadeSystem（接近の開始）。
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

    // 回避の無敵時間（秒）。isInvincible が立っている間、被弾処理が見る
    // HealthComponent.invincibilityTimer を毎フレームこの値以上に保ち、ダメージやドローンとの接触を無効にする。
    // 回避（ダッシュ/接近）が終わっても、この秒数だけ無敵が残る。
    float invincibleWindow = 0.06f;

    // --- 接近の設定値 ---
    // ジャスト回避が成立すると世界がスローになり、その間にボスへ近づく。
    // 攻撃が届く位置まで移動するための時間で、攻撃の時間ではない。
    float approachSpeed       = 60.0f; // 接近時の移動速度（m/秒）。通常のダッシュより速くする
    float approachDuration    = 0.35f; // 接近を行う時間（秒・実時間）。ヒットストップが明けてから数える
    float approachPlayerScale = 1.0f;  // 接近中のプレイヤーの時間倍率。世界スローを打ち消した後に掛ける（1.0=実時間、>1で速い）
    float approachStopRange   = 4.0f;  // ボスとの水平距離がこの値（m）以下になったら接近を終える（めり込み防止）

    // --- 実行時の状態 ---
    bool isDashing    = false;
    bool isInvincible = false;

    // ジャスト回避を受け付けている間 true
    bool isJustEvadeWindowActive = false;

    bool  isApproaching = false; // 接近中か
    float approachTimer = 0.0f;  // 接近の残り時間（秒）

    float                        currentTimer  = 0.0f;      // ダッシュ中は残り時間、ダッシュ外はクールダウンの残り（秒）
    DirectX::SimpleMath::Vector3 dashDirection = {0, 0, 0}; // ダッシュ・接近の移動方向（正規化済み）
};