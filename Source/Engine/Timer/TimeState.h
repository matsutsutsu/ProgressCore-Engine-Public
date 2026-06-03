#pragma once
#include <cstdint>
#include <algorithm>
// ---------------------------------------------------------
// [時間グループ定義]
// 各エンティティがどの時間軸に属しているかを示すフラグ
// ---------------------------------------------------------
enum class TimeGroup : uint8_t 
{
    None = 0,
    Player = 1,
    Enemy = 2,
    Environment = 3,
    UI = 4
};

struct SlowMotion {
    float timer = 0.0f;
    float duration = 0.0f;
    float targetScale = 1.0f;
    float currentComputedScale = 1.0f; // 計算済みのスケールを保持

    // 更新処理：タイマーを進め、現在のスケールを計算する
    void Update(float dt) {
        if (timer > 0.0f) {
            timer -= dt;
            float progress = 1.0f - ((std::max)(timer / duration, 0.0f));
            // EaseOut曲線で滑らかに元に戻す
            float easeOut = 1.0f - (1.0f - progress) * (1.0f - progress);
            currentComputedScale = std::lerp(targetScale, 1.0f, easeOut);
        }
        else {
            timer = 0.0f;
            currentComputedScale = 1.0f;
        }
    }
};

// ---------------------------------------------------------
// [世界の親時計 (World Resource)]
// O(1)でアクセス可能なグローバルな時間スケール設定
// ---------------------------------------------------------
struct TimeContext 
{
    // 元の世界の速度 (通常1.0)
    float baseGlobalScale = 1.0f;

    // 実際に各システムが読むスケール
    float globalScale = 1.0f;

    // ★追加: オーディオや演出が参照すべき「ヒットストップのノイズを含まない純粋なスケール」
    float cinematicScale = 1.0f;

    // --- 全体ヒットストップ（ヒットスロー）用 ---
    float globalHitStopTimer = 0.0f;
    float globalFreezeScale = 1.0f;


    // 各勢力ごとの独立したスロー状態
    SlowMotion playerSlow;
    SlowMotion enemySlow;
    SlowMotion worldSlow;
 
    float playerScale = 1.0f;  // プレイヤー勢力専用
    float enemyScale = 1.0f;   // 敵勢力専用
    float cameraScale = 1.0f;  // カメラ専用
};

// ---------------------------------------------------------
// [各自の腕時計 (Component)]
// ★重要: 動く全てのエンティティのArchetypeに最初から積んでおく
// ---------------------------------------------------------
struct TimeState {
    // 各システム（移動、アニメ等）がReadする「計算済みの実効デルタタイム」
    float localDt = 0.016f;

    // --- 以下、TimeScaleSystem専用の内部状態 ---
    float hitStopTimer = 0.0f;  // 0.0fなら平常。リアル時間(rawDt)で減算される
    float freezeScale = 0.0f;  // ヒットストップ中の進み具合（0=完全停止）
    TimeGroup group = TimeGroup::None;
};