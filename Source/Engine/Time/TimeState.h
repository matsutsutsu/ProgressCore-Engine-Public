#pragma once
#include <cstdint>

// ---------------------------------------------------------
// [時間グループ定義]
// ---------------------------------------------------------
enum class TimeGroup : uint8_t
{
    None        = 0,
    Player      = 1,
    Enemy       = 2,
    Environment = 3,
    UI          = 4,
    Count // 配列の要素数として使用
};

// ---------------------------------------------------------
// [持続的スローデータ (POD)]
// ※メソッドを排除し、純粋なデータ構造（シリアライズ可能）に変更
// ---------------------------------------------------------
struct SlowMotionData
{
    float timer                = 0.0f;
    float duration             = 0.0f;
    float targetScale          = 1.0f;
    float currentComputedScale = 1.0f;
};

// ---------------------------------------------------------
// [世界の親時計 (World Resource)]
// ---------------------------------------------------------
struct TimeContext
{
    // 全体の基本初期スケール（通常は1.0f、ゲーム全体の速度調整に使用）
    float baseGlobalScale = 1.0f;
    float globalScale     = 1.0f; // TimeScaleSystem が毎フレーム算出する実効の全体倍率（読み取り用）

    // --- 全体ヒットストップ（完全停止系）。Time::HitStop が設定する ---
    float globalHitStopTimer = 0.0f;
    float globalFreezeScale  = 1.0f;

    // --- 全体スロー（イーズ復帰）。撃破スロー/切り払い準備など。Time::SlowWorld / SustainWorldSlow が設定 ---
    //   globalScale に乗るため group に依らず、TimeStateComponent を持つ全エンティティが滑らかにスローになる。
    SlowMotionData cinematicSlow;

    // 勢力別の倍率（groupScale レイヤー）。現状は全て1.0（＝無効）。
    //   将来「敵だけスロー」等を足す時はここへ書く層を用意する（Time::SlowGroup など）。
    float groupScales[static_cast<size_t>(TimeGroup::Count)] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    // --- デバッグ用（コマ送り）。Time::Pause / Resume / StepFrame が設定 ---
    bool isPaused      = false;
    bool stepNextFrame = false;
};

// ---------------------------------------------------------
// [各自の腕時計 (Component)]
// ---------------------------------------------------------
struct TimeStateComponent
{
    float localDt = 0.016f;

    // 【レイヤー1】: 瞬間的な完全停止（ヒットストップ）
    float hitStopTimer = 0.0f;
    float freezeScale  = 0.0f;

    // 【レイヤー2】: 持続的なバフ/デバフ（魔法等による個別スロー）
    float buffScale = 1.0f;

    // 【レイヤー3】: 演出用（自分だけ動く、クロックアップ等）
    float cinematicScale = 1.0f;

    TimeGroup group = TimeGroup::None;
};