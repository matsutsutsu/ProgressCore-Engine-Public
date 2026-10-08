#pragma once
// ===========================================================================
// 概要: プレイヤーの特殊アクション「切り払い」の設定値と実行時の状態を持つ Component。
//
// 責務:
//   - 切り払いのフェーズ（待機・準備・突進）を持ち、攻撃ボタンをどの System が処理するかを決める
//   - 突進・扇の当たり判定・ドローンの吹き飛ばしの設定値を持つ
//   - チェイン段ごとのスロー倍率と準備の猶予を持つ
//   - ゲージとストックを持つ
//
// 備考: 状態を書くのは PlayerCleaveSystem。
// ===========================================================================
#include <SimpleMath.h>
#include <cstdint> // uint8_t

// 切り払いのフェーズ。Idle=待機/クールダウン, Ready=準備(スロー・照準), Lunging=突進中。
//
// 攻撃ボタン(左クリック)をどの System が処理するかもこのフェーズで決まる。
// 同じ PlayerInputComponent.attackPressed を2つの System が読むため、
// どちらが処理するかをここで排他的に決める。
//
//   フェーズ  | 攻撃ボタンの意味   | 処理するシステム
//   ----------|--------------------|---------------------------------
//   Idle      | 通常/ダッシュ/空中 | PlayerAttackSystem (L01_Input)
//   Ready     | 切り払いの突進     | PlayerCleaveSystem (L02_PostUpdate)
//   Lunging   | 無視               | （どちらも処理しない）
//
// このボタンを読む System を足すときは、この表に従う。
// System ごとに押下フラグを持たせて意味を分けない（同じボタンの解釈が System ごとにずれるため）。
enum class CleavePhase : uint8_t
{
    Idle    = 0,
    Ready   = 1,
    Lunging = 2
};

/**
 * @brief 切り払いの設定値と実行時の状態。
 *
 * 右クリックで準備(Ready)に入り、世界全体をスローにして画面をモノクロにする。
 * 左クリックでカメラの向いた3D方向へ突進し、前方の扇にいるドローンを壊さずに吹き飛ばす。
 * 1体以上に当たり、ストックが残っていれば再び準備に戻る（チェイン）。
 *
 * @note ストックは1突進で1消費する。上限 maxStock がそのままチェインの上限になる。
 */
struct CleaveComponent
{
    // --- 設定値 ---
    float cleaveSpeed    = 60.0f; // 突進速度（m/秒）。ダッシュより速くする
    float cleaveDuration = 0.18f; // 突進時間（秒）。速度×時間 ≒ 突進距離
    float cleaveCooldown = 0.4f;  // チェインが切れた後、再び準備に入れるまでの待ち時間（秒）
    float fanRange       = 6.0f;  // 前方扇の半径（m）
    float fanHalfAngle   = 45.0f; // 扇の半開き角（度）。±この角度が判定内
    float knockbackSpeed = 28.0f; // ドローンを進行方向へ吹き飛ばす初速（m/秒）
    float knockbackTime  = 2.0f;  // 吹き飛ばしてから隊列へ戻るまでの時間（秒）。この間は再ヒットしない

    // 準備中の全体スロー（Time::SustainWorldSlow。プレイヤー自身も遅くなる）
    float slowDuration = 0.35f; // 準備を抜けた後、等速へ戻るまでのイーズ時間（秒）

    // チェイン段ごとのスロー倍率と準備の猶予を、段1/2/3の3つの値で持つ。
    //   チェインはストックで最大3段になるので、式ではなく値を直接置く。
    //   段が進むほど倍率を上げ（スローが弱まる）、猶予を短くする。
    //   readyDuration3 は、最終段でも人が判断できる最低限の秒数にする。
    float slowScale1 = 0.10f, slowScale2 = 0.25f, slowScale3 = 0.40f;           // 準備中の全体タイムスケール
    float readyDuration1 = 1.5f, readyDuration2 = 0.95f, readyDuration3 = 0.6f; // 準備の自動解除までの時間（秒・実時間）
    bool  enableStageDecay = true;                                              // false なら段に関係なく常に段1の値を使う

    // --- ゲージとストック ---
    float maxGauge         = 100.0f; // ストック1つ分のゲージ量
    int   maxStock         = 3;      // ストック上限（＝チェイン上限）
    float gaugeOnJustEvade = 50.0f;  // ジャスト回避成立で加算する量（主な供給源）
    float gaugeOnDash      = 8.0f;   // 通常のダッシュ開始で加算する量

    // --- 実行時の状態 ---
    CleavePhase phase      = CleavePhase::Idle;
    float       phaseTimer = 0.0f;  // Ready:残り猶予 / Lunging:残り突進時間 / Idle:クールダウンの残り（秒）
    int         hitCount   = 0;     // 直前の突進で当てたドローン数（0ならチェイン終了）
    int         chainCount = 0;     // 現在のチェイン段数（当てるたびに+1、解除で0）
    float       gauge      = 0.0f;  // 現在のゲージ量（次のストックまで）
    int         stock      = 3;     // 現在のストック数（0なら切り払い不可）。開始時は満タン
    bool        wasDashing = false; // 前フレームのダッシュ状態。ダッシュ開始の検出に使う（入力の押下とは別）

    DirectX::SimpleMath::Vector3 direction = {0.0f, 0.0f, 1.0f}; // 突進方向（カメラ前方・3D）
};
