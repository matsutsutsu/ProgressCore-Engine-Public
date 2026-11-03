#pragma once
// ===========================================================================
// 概要: ラッシュ中のカメラ演出（寄りと引き）の設定値と、演出中の状態を持つ Component。
//
// 責務:
//   - ラッシュのフェーズごとの構図（距離・画角・角度・傾き・敵への注視・追従・寄せ方）を持つ
//   - 回避成立・斬りの瞬間に出す揺れと画角パンチの強さを持つ
//   - 今カメラへ出している構図（ばねで寄せている途中の値と速さ）と、終了時のフェードの状態を持つ
//
// 備考: 構図の値はすべて普段のカメラへの「足し込み量」。ロジックは PlayerRushCameraSystem が持つ。
//       フェーズそのものは PlayerRushComponent が持ち、この Component は読むだけ。
//       既定値は ZZZ 風の解釈：成立で引いて傾け、待機は低い 3/4 で敵を見上げ、反撃は寄りながら画角を広げて付いていく。
// ===========================================================================
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // CameraFramingParams
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"  // RushPhase

// フェーズ1つ分の構図。寄せ方以外の値はすべて普段のカメラへの足し込み量。
struct RushCameraShot
{
    float distance    = 0.0f; // 距離の足し込み（m）。負で寄る、正で引く
    float fov         = 0.0f; // 画角の足し込み（度）
    float pitch       = 0.0f; // ピッチの足し込み（度）。負で上から見下ろす、正で低く見上げる
    float yaw         = 0.0f; // ヨーの足し込み（度）。斜めから映す
    float roll        = 0.0f; // 画面の傾き（度）。ダッチアングル
    float focus       = 0.0f; // 注視点を敵へ寄せる割合（0〜1）
    float followBoost = 0.0f; // 追従の速さの足し込み（1/秒）。突進に遅れず付いていく
    float blendTime   = 0.1f; // この構図へ寄せきる時間（秒）
    float bounce      = 0.0f; // 寄せたときの行き過ぎ（0=行き過ぎない、0.5 で約14%行き過ぎて戻る）。上げるほど早く届く
};

struct PlayerRushCameraComponent
{
    // --- 設定：フェーズごとの構図 ---
    // 回避離脱＝勢いよく引いて傾ける。行き過ぎて戻る反動で「止まった」瞬間を作る
    RushCameraShot locked = {.distance = 3.0f, .fov = 12.0f, .pitch = -6.0f, .yaw = 18.0f, .roll = 6.0f,
                             .focus = 0.3f, .blendTime = 0.12f, .bounce = 0.5f};

    // 待機＝低い 3/4 アングルで寄り、敵を見上げる。傾きは逆へ少し返す
    RushCameraShot ready = {.distance = -1.8f, .fov = -6.0f, .pitch = 6.0f, .yaw = 28.0f, .roll = -3.0f,
                            .focus = 0.55f, .blendTime = 0.18f, .bounce = 0.2f};

    // 攻撃＝寄りながら画角を広げる（速さが出る）。追従を締めて突進に付いていく
    RushCameraShot attack = {.distance = -2.2f, .fov = 4.0f, .pitch = 2.0f, .yaw = 8.0f, .roll = 0.0f,
                             .focus = 0.75f, .followBoost = 20.0f, .blendTime = 0.07f, .bounce = 0.4f};

    // 連撃＝中間へゆっくり戻す
    RushCameraShot flurry = {.distance = 0.8f, .focus = 0.2f, .blendTime = 0.45f};

    // --- 設定：終了・ワンショット ---
    float releaseTime     = 0.6f;  // ラッシュ終了後、普段の構図へ戻しきる時間（秒）
    bool  yawFollowsEvade = true;  // ヨーと傾きの向きを回避方向で左右反転する（避けた側の反対から映す）
    float startShake      = 0.35f; // 回避成立の瞬間の揺れ（0〜1）
    float strikeFovPunch  = 10.0f; // 斬った瞬間に画角を一瞬広げる量（度）
    float strikeShake     = 0.5f;  // 斬った瞬間の揺れ（0〜1）

    // --- 状態（実行時） ---
    CameraFramingParams current{};                // 今カメラへ出している構図（ばねで寄せている途中の値）
    CameraFramingParams velocity{};               // current の各値が動いている速さ（ばねの状態。focusTarget は使わない）
    float               weight         = 0.0f;  // 構図の効き（1=全開、終了後に 0 へ落ちる）
    float               yawSign        = 1.0f;  // ヨーと傾きの左右（開始時に回避方向から決める）
    bool                wasActive      = false; // 前フレームにラッシュ中だったか（開始の検出）
    bool                wasAttackFired = false; // 前フレームに斬り済みだったか（斬りの瞬間の検出）
};
