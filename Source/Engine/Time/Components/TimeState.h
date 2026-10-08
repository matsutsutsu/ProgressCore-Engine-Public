#pragma once
// ===========================================================================
// 概要: 時間スケールのデータ定義（TimeGroup / TimeContext / TimeStateComponent）。
//
// 責務:
//   - TimeContext: 世界全体の時計（World リソース）
//   - TimeStateComponent: Entity ごとの時計と、その実効 dt（localDt）
//
// 備考: 値の設定は Time ファサード（TimeService.h）、dt の合成は TimeScaleSystem が行う。
//       ここはデータだけを持つ。
// ===========================================================================
#include <cstdint>

// 勢力ごとに時間倍率を変えるためのグループ
enum class TimeGroup : uint8_t
{
    None        = 0,
    Player      = 1,
    Enemy       = 2,
    Environment = 3,
    UI          = 4,
    Count // 配列の要素数として使う
};

// イーズ付きで 1.0 へ戻るスローの状態。関数は持たず、更新は TimeScaleSystem が行う
struct SlowMotionData
{
    float timer                = 0.0f; // 残り時間（秒）
    float duration             = 0.0f; // 全体の長さ（秒）。timer との比でイーズの進行度を出す
    float targetScale          = 1.0f; // スロー開始時の倍率
    float currentComputedScale = 1.0f; // 今の倍率（TimeScaleSystem が計算する）
};

// 世界全体の時計（World リソース）
struct TimeContext
{
    // ゲーム全体の基本倍率（通常は 1.0。全体の速度調整に使う）
    float baseGlobalScale = 1.0f;
    float globalScale     = 1.0f; // TimeScaleSystem が毎フレーム算出する実効の全体倍率（読み取り用）

    // 全体ヒットストップ（完全停止系）。Time::HitStop が設定する
    float globalHitStopTimer = 0.0f; // 残り時間（秒）
    float globalFreezeScale  = 1.0f; // 停止中の倍率（0 で完全停止）

    // 全体スロー（イーズ復帰）。撃破スロー/切り払い準備など。Time::SlowWorld / SustainWorldSlow が設定する。
    //   globalScale に乗るため group に依らず、TimeStateComponent を持つ全エンティティが滑らかにスローになる。
    SlowMotionData cinematicSlow;

    // 勢力別の倍率（groupScale レイヤー）。現在は全て 1.0（＝無効）。
    //   「敵だけスロー」等を足すときは、ここへ書く API（Time::SlowGroup など）を用意する。
    float groupScales[static_cast<size_t>(TimeGroup::Count)] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    // 一時停止・コマ送り（デバッグ用）。Time::Pause / Resume / StepFrame が設定する
    bool isPaused      = false;
    bool stepNextFrame = false;
};

// Entity ごとの時計。各 System は dt の代わりに localDt を使う
struct TimeStateComponent
{
    float localDt = 0.016f; // この Entity の実効 dt（秒）。TimeScaleSystem が毎ステップ書く

    // 【レイヤー1】: 瞬間的な完全停止（ヒットストップ）。他の倍率より優先して上書きする
    float hitStopTimer = 0.0f; // 残り時間（秒）
    float freezeScale  = 0.0f; // 停止中の倍率（0 で完全停止）

    // 【レイヤー2】: 持続的なバフ/デバフ（魔法等による個別スロー）
    float buffScale = 1.0f;

    // 【レイヤー3】: 演出用（自分だけ動く、クロックアップ等）
    float cinematicScale = 1.0f;

    TimeGroup group = TimeGroup::None;
};