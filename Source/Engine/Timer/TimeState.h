#pragma once
#include <cstdint>

// ---------------------------------------------------------
// [時間グループ定義]
// ---------------------------------------------------------
enum class TimeGroup : uint8_t {
    None = 0,
    Player = 1,
    Enemy = 2,
    Environment = 3,
    UI = 4,
    Count // 配列の要素数として使用
};

// ---------------------------------------------------------
// [持続的スローデータ (POD)]
// ※メソッドを排除し、純粋なデータ構造（シリアライズ可能）に変更
// ---------------------------------------------------------
struct SlowMotionData {
    float timer = 0.0f;
    float duration = 0.0f;
    float targetScale = 1.0f;
    float currentComputedScale = 1.0f;
};

// ---------------------------------------------------------
// [世界の親時計 (World Resource)]
// ---------------------------------------------------------
struct TimeContext {
	// 全体の基本初期スケール（通常は1.0f、ゲーム全体の速度調整に使用）
    float baseGlobalScale = 1.0f;
    float globalScale = 1.0f;
    float cinematicScale = 1.0f;

    // --- 全体ヒットストップ（完全停止系） ---
    float globalHitStopTimer = 0.0f;
    float globalFreezeScale = 1.0f;

    // --- 持続的スロー（バフ・演出系） ---
    SlowMotionData playerSlow;
    SlowMotionData enemySlow;
    SlowMotionData worldSlow;

    // ★追加: 勢力別の倍率を配列化（O(1)アクセス・分岐予測ペナルティ排除用）
    // 初期値はすべて1.0f
    float groupScales[static_cast<size_t>(TimeGroup::Count)] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    // --- デバッグ用（コマ送り） ---
    bool isPaused      = false;
    bool stepNextFrame = false;
};

// ---------------------------------------------------------
// [各自の腕時計 (Component)]
// ---------------------------------------------------------
struct TimeStateComponent {
    float localDt = 0.016f;

    // 【レイヤー1】: 瞬間的な完全停止（ヒットストップ）
    float hitStopTimer = 0.0f;
    float freezeScale = 0.0f;

    // 【レイヤー2】: 持続的なバフ/デバフ（魔法等による個別スロー）
    float buffScale = 1.0f;

    // 【レイヤー3】: 演出用（自分だけ動く、クロックアップ等）
    float cinematicScale = 1.0f;

    TimeGroup group = TimeGroup::None;
};