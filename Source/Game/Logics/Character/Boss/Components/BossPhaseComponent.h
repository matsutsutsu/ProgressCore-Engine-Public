#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// 並びを変えたら kBossPhaseNames も同じ並びに直す
enum class BossPhase : uint8_t
{
    Combat,         // 第1形態
    Dead,           // 死亡。ここからは抜けない
    Transition,     // 形態移行の演出中
    Combat2,        // 第2形態
    TutorialAttack, // チュートリアル：手加減して攻める
    TutorialWait,   // チュートリアル：攻撃を出さずに待つ
};

inline constexpr const char *kBossPhaseNames[] = {
    "Combat",
    "Dead",
    "Transition",
    "Combat2",
    "TutorialAttack",
    "TutorialWait",
};
inline constexpr size_t kBossPhaseCount = sizeof(kBossPhaseNames) / sizeof(kBossPhaseNames[0]);

// 形態の表示名を返す。範囲外の値なら "?"
inline const char *ToString(BossPhase p)
{
    const size_t i = static_cast<size_t>(p);
    return (i < kBossPhaseCount) ? kBossPhaseNames[i] : "?";
}

// ボスの形態。形態を決めるのは BossPhaseSystem
struct BossPhaseComponent
{
    std::string phase2AssetPath;               // 第2形態の BT。空なら移行しない
    float       phase2HealthThreshold = 50.0f; // 移行する HP の割合（0〜100）
    std::string tutorialAssetPath;             // チュートリアルの BT。空なら本来の BT で戦う

    // --- 移行演出 ---
    float transitionDuration = 2.5f;  // 演出の長さ [秒]。この間は無敵で BT が止まる
    float finishSlowScale    = 0.10f; // とどめのスローの時間の速さ（0 で止まる、1 で等速）
    float finishSlowDuration = 0.85f; // とどめのスローの長さ [実時間の秒]。明けたら移行ムービー

    // --- 撃破演出 ---
    float defeatSlowScale    = 0.10f; // 倒したときのスローの時間の速さ（0 で止まる、1 で等速）
    float defeatSlowDuration = 0.85f; // 倒したときのスローの長さ [実時間の秒]。明けたら撃破ムービー

    BossPhase phase = BossPhase::Combat; // 今の形態
    float     timer = 0.0f;              // 形態の残り時間 [秒]（移行演出）

    float movieDelayTimer = 0.0f; // 移行ムービーを頼むまでの残り [実時間の秒]。0 以下なら待っていない

    // デバッグ: 移行演出を飛ばして第2形態へ移る要求（保存しない）
    // Boss Debugger が立て、BossPhaseSystem が受けて下ろす
    bool debugSkipToPhase2 = false;
};
