#pragma once
// ===========================================================================
// 概要: ジャスト回避成立後の「ラッシュ」の設定値と状態を持つ Component。
//
// 責務:
//   - 世界スローの深さ（ラッシュ中は一律）と、プレイヤーの時間倍率を持つ
//   - 操作状態のフェーズ（操作不能/準備/自由）と、その残り時間を持つ
//   - ラッシュ中の攻撃で敵へ踏み込む「間合い詰め」の設定と状態を持つ
//
// 備考: ロジック（開始・フェーズ遷移・時間制御・終了・間合い詰め）は PlayerRushSystem が持つ。
//       画面演出は JustEvadeEffectComponent 側。
// ===========================================================================
#include <cstdint>
#include <SimpleMath.h>
#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID

// ラッシュ中の「プレイヤーの操作権限」を表すフェーズ。成立直後から順に進む。
// スローの深さはフェーズに依らず一律（worldScale）で、フェーズは操作だけを切り替える。
//   Locked: ジャスト回避の演出中。入力を受けず、その場で止まる（キメの一瞬）。
//   Ready : 間合い詰めの受付窓。攻撃を押すと敵へ詰めて斬る。移動は普通にできる。
//   Free  : 自由操作。移動・通常攻撃はできるが、敵へ自動で詰める特別攻撃は無い。
enum class RushPhase : uint8_t
{
    Locked = 0, // 操作不能：成立直後の一瞬
    Ready,      // 準備：間合い詰め攻撃を受け付ける窓
    Free,       // 自由：通常操作（詰めアシスト無し）
};

struct RushComponent
{
    // --- 設定値：世界スロー（ラッシュ中は一律） ---
    float worldScale  = 0.1f; // 世界をどこまで遅くするか（1.0=等速）
    float playerScale = 1.0f; // プレイヤーの時間倍率（1.0=実時間）
    float slowRecover = 0.4f; // 終了後、等速へ戻るイーズ時間（秒）

    // --- 設定値：フェーズごとの長さ（秒） ---
    float lockedDuration = 0.15f; // 操作不能の長さ（秒）
    float readyDuration  = 0.45f; // 準備（詰め受付）の長さ（秒）
    float freeDuration   = 0.60f; // 自由操作の長さ（秒）

    // --- 設定値：間合い詰め（準備中の攻撃で敵へ踏み込む） ---
    float closeInSpeed        = 90.0f; // 踏み込みの速さ（m/秒）
    float closeInStopDistance = 3.0f;  // ロックオン時、敵のこの距離まで来たら止まる（m）
    float closeInMaxTime      = 0.25f; // 踏み込みを続ける最大時間（秒）。暴走防止の上限

    // --- 実行時の状態 ---
    bool      isActive = false;             // 今ラッシュ中か
    RushPhase phase    = RushPhase::Locked; // 今のフェーズ
    float     timer    = 0.0f;              // 今のフェーズの残り時間（秒）

    float                        closeInTimer  = 0.0f;      // 踏み込みの残り時間（秒）
    DirectX::SimpleMath::Vector3 closeInDir    = {0, 0, 0}; // 踏み込む向き（正規化済み）
    CCL::ECS::EntityID           closeInTarget = CCL::ECS::InvalidEntityID; // ロックオン時の詰め先。無ければ Invalid
};
