#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュと、ジャスト回避成立後のラッシュの設定値と状態を持つ Component。
//
// 責務:
//   - ダッシュの速度・時間・クールダウンと、ジャスト回避の受付時間を持つ
//   - 回避中の無敵時間を持つ
//   - ジャスト回避成立後のラッシュ（踏み込み＋集中スロー）の設定値と状態を持つ
//
// 備考: 状態を書くのは PlayerDashSystem と JustEvadeSystem（ラッシュの開始）。
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

    // --- ラッシュ（ジャスト回避成立後）の設定値 ---
    // BotWのラッシュ風。成立すると世界が目に見えてスローになり、回避方向へ“ぎゅいん”と踏み込む(A)。
    // 続けて少しスローのまま自由行動できる集中窓(B)に入る。時間はすべて実時間(rawDt)で数える。
    float flurryLungeSpeed    = 70.0f; // A:踏み込みの最大速度（m/秒）。減少カーブで ease-out にする
    float flurryLungeDuration = 0.28f; // A:踏み込みの長さ（秒・実時間）
    float flurryPlantDuration = 0.14f; // B:踏み込み後に“ピタッ”と止めて見せる固定の長さ（秒・実時間）。メリハリの要
    float flurryPlantFreeze   = 0.0f;  // B:固定中の世界フリーズ倍率（0=完全停止）。別レイヤーの全体ヒットストップで止める
    float flurryFocusDuration = 1.2f;  // C:モノクロで自由行動できる集中スローの長さ（秒・実時間）
    float flurryWorldScale    = 0.2f;  // A/C共通の世界スロー倍率。SustainWorldSlowは強い方が勝つので単一値にする
    float flurryPlayerScale   = 1.0f;  // プレイヤー自身の時間倍率。世界スローを打ち消した後に掛ける（1.0=実時間、>1で自分だけ速い）
    float flurrySlowRecover   = 0.4f;  // 窓終了後、世界スローが等速へ戻るまでのイーズ時間（秒）

    // --- 実行時の状態 ---
    bool isDashing    = false;
    bool isInvincible = false;

    // ジャスト回避を受け付けている間 true
    bool isJustEvadeWindowActive = false;

    bool  isFlurryLunging  = false; // A:踏み込み中（操作ロック・速度上書き）
    bool  isFlurryPlanting = false; // B:固定で止めて見せる中（操作ロック・全体フリーズ）
    bool  isFlurryFocusing = false; // C:モノクロ集中スロー中（操作自由・速度上書きしない）
    float flurryTimer      = 0.0f;  // 現フェーズの残り時間（秒・実時間）

    float                        currentTimer  = 0.0f;      // ダッシュ中は残り時間、ダッシュ外はクールダウンの残り（秒）
    DirectX::SimpleMath::Vector3 dashDirection = {0, 0, 0}; // ダッシュ・接近の移動方向（正規化済み）
};