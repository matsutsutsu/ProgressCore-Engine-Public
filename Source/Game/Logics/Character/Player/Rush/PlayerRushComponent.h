#pragma once
// ===========================================================================
// 概要: ジャスト回避成立後の「ラッシュ」の設定値と状態を持つ Component。
//
// 責務:
//   - 世界スローの深さ（ラッシュ中は一律）と、プレイヤーの時間倍率を持つ
//   - 操作/演出のフェーズ（回避離脱/待機/攻撃/自由）と、その残り時間を持つ
//   - 回避離脱の向き・速さ、間合い詰めの設定、無敵の維持量を持つ
//
// 備考: ロジック（開始・フェーズ遷移・時間制御・終了）は PlayerRushSystem が持つ。
//       画面演出は PlayerRushEffectComponent 側。
// ===========================================================================
#include <cstdint>
#include <SimpleMath.h>
#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID

// ラッシュの進行フェーズ。成立直後から順に進む。
// スローはフェーズに依らず一律で、フェーズは「操作」と「演出」だけを切り替える。
//   Locked: 回避演出。ダッシュを中断し、回避方向へ EaseOut で大きく離脱する。操作不能。
//   Ready : 待機。操作不能で、攻撃入力だけ受け付ける。攻撃で Attack へ、無攻撃で時間切れなら Free へ。
//   Attack: 間合いを詰めて攻撃する一連。終わったら Free へ。
//   Free  : 自由。スローの中で追撃・移動ができる。時間切れでラッシュ終了。
enum class RushPhase : uint8_t
{
    Locked = 0, // 回避離脱（操作不能）
    Ready,      // 攻撃待機（攻撃入力のみ）
    Attack,     // 間合い詰め攻撃
    Free,       // 自由行動
};

struct PlayerRushComponent
{
    // --- 設定：世界スロー（ラッシュ中は一律） ---
    float worldScale  = 0.1f; // 世界をどこまで遅くするか（1.0=等速）
    float playerScale = 1.0f; // プレイヤーの時間倍率（1.0=実時間）
    float slowRecover = 0.4f; // 終了後、等速へ戻るイーズ時間（秒）

    // --- 設定：フェーズの長さ（秒） ---
    float lockedDuration = 0.25f; // 回避離脱の長さ
    float readyDuration  = 0.40f; // 待機の長さ（この間に攻撃しなければ Free へ）
    float attackDuration = 0.35f; // 攻撃状態の長さ（詰め＋攻撃）
    float freeDuration   = 0.80f; // 自由行動の長さ

    // --- 設定：回避離脱（Locked） ---
    float leapSpeed = 120.0f; // 離脱の初速（m/秒）。EaseOut で 0 へ落ちる

    // --- 設定：間合い詰め（Attack） ---
    float closeInSpeed        = 90.0f; // 詰めの速さ（m/秒）
    float closeInStopDistance = 3.0f;  // ロックオン時、敵のこの距離まで来たら止まる（m）

    // --- 設定：無敵 ---
    float invincibleRefresh = 0.1f; // ラッシュ中、毎フレーム維持する無敵の残り（秒）

    // --- 状態 ---
    bool      isActive    = false;          // 今ラッシュ中か
    RushPhase phase        = RushPhase::Locked; // 今のフェーズ
    float     timer        = 0.0f;          // 現フェーズの残り時間（秒）
    bool      attackFired  = false;         // Attack で斬りを出したか（詰め切ってから1回だけ出す）

    DirectX::SimpleMath::Vector3 evadeDir      = {0, 0, 0}; // 回避離脱の向き（＝ダッシュ方向・正規化済み）
    DirectX::SimpleMath::Vector3 closeInDir    = {0, 0, 0}; // 間合い詰めの向き（正規化済み）
    CCL::ECS::EntityID           closeInTarget = CCL::ECS::InvalidEntityID; // ロックオン時の詰め先。無ければ Invalid
};
