// ボスの「大状態」を保持するコンポーネント。ボスの3層の一番上で、BTの差し替えと
// 入場演出を担う（その中で技を選ぶのがBT、物理へ変換するのが BossAction）。
//
// 第2形態のBTパスとHP閾値はここに持つ。BehaviorTreeComponent は汎用データの置き場
// なので、ボスにしか無い概念を置くと敵を増やすたびにフィールドが溜まる。
//
// 形態移行: Combat --HP<=閾値--> Transition --演出時間経過--> Combat2
//   Transition の間はBTを止め、無敵にし、群れを頭上へ退避させる。攻撃が途切れて
//   仕切り直す間ができ、形態が変わったことが画面で分かる。
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// 並び順がそのままインスペクタの列挙値になる。増やすときは末尾に足すこと
// （途中に挿すと保存済みプレハブの値がずれる）。
enum class BossPhase : uint8_t
{
    Combat,     // 第1形態。BT（プレハブの assetPath）が技を選ぶ
    Dead,       // 死亡。終端状態。ここからは抜けない
    Transition, // 形態移行の演出中。BTを止め、無敵になり、群れを頭上へ退避させる
    Combat2,    // 第2形態。BT を phase2AssetPath のものへ差し替えて戦う
};

// 表示名。名前の二重管理を避けるため enum の隣に置く。
inline constexpr const char *kBossPhaseNames[] = {
    "Combat",
    "Dead",
    "Transition",
    "Combat2",
};
inline constexpr size_t kBossPhaseCount = sizeof(kBossPhaseNames) / sizeof(kBossPhaseNames[0]);

inline const char *ToString(BossPhase p)
{
    const size_t i = static_cast<size_t>(p);
    return (i < kBossPhaseCount) ? kBossPhaseNames[i] : "?";
}

struct BossPhaseComponent
{
    // 第2形態で使うBTのパス。空なら形態移行そのものをしない（設定漏れの安全弁）。
    std::string phase2AssetPath;

    // 第2形態へ移行するHP割合（0〜100）。
    float phase2HealthThreshold = 50.0f;

    // 現在の大状態。書き手は BossPhaseSystem のみ。滞在タイマーは入場時にセットし毎フレーム減る。
    BossPhase phase = BossPhase::Combat;

    float timer = 0.0f;
};
