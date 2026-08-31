/**
 * @file BossPhaseComponent.h
 * @brief ボスの「大状態」（戦闘中／死亡）を保持するコンポーネント。
 *
 * 位置づけ（ボスの3層のうち一番上）:
 *   BossPhase（このコンポーネント） … 大きく何をしているか。BTの差し替えと入場演出を担う
 *   ボスBT                          … その中で具体的な技を選ぶ
 *   BossAction                      … 選ばれた技を物理に変換する
 *
 * 汎用のステートマシン機構を使わない理由:
 *   状態が2つで、ボス1体しか存在しない。木構造やノード登録の仕組みを噛ませると
 *   実装コストと間接化だけが増えて、データ駆動の利点（再ビルドなしの編集）は
 *   BT側で既に得られている。敵が増えたら「敵ごとに小さく書く」で足りる。
 *
 * @note 調整パラメータはここに持たせない。第2形態のBTパスとHP閾値は
 *       BehaviorTreeComponent の phase2AssetPath / phase2HealthThreshold を使う
 *       （既にプレハブでシリアライズ済みのフィールドを再利用する）。
 *
 * 形態移行の流れ:
 *   Combat --HP<=閾値--> Transition --演出時間経過--> Combat2
 *   Transition の間は BT を止め、無敵にし、群れを頭上へ退避させる。
 *   これで「攻撃が途切れて仕切り直す」間ができ、形態が変わったことが画面で分かる。
 */
#pragma once
#include <cstdint>
#include <cstddef>

// ===================================================================================
// [ 列挙型 ] ボスの大状態
// ===================================================================================
/// @note 並び順がそのままインスペクタの列挙値になるので、途中に挿すと
///       保存済みプレハブの値がずれる。増やすときは末尾に足すこと。
enum class BossPhase : uint8_t
{
    Combat,     ///< 第1形態。BT（プレハブの assetPath）が技を選ぶ
    Dead,       ///< 死亡。終端状態。ここからは抜けない
    Transition, ///< 形態移行の演出中。BTを止め、無敵になり、群れを頭上へ退避させる
    Combat2,    ///< 第2形態。BT を phase2AssetPath のものへ差し替えて戦う
};

/// @brief 表示名。インスペクタの列挙表示とデバッグ頭上表示で共有する
///        （名前の二重管理を避けるため、enum と同じ場所に置く）。
inline constexpr const char *kBossPhaseNames[] = {
    "Combat", "Dead", "Transition", "Combat2",
};
inline constexpr size_t kBossPhaseCount = sizeof(kBossPhaseNames) / sizeof(kBossPhaseNames[0]);

/// @brief BossPhase を表示用文字列にする。範囲外は "?" を返す。
inline const char *ToString(BossPhase p)
{
    const size_t i = static_cast<size_t>(p);
    return (i < kBossPhaseCount) ? kBossPhaseNames[i] : "?";
}

// ===================================================================================
// [ 構造体 ] ボス大状態データ (POD)
// ===================================================================================
struct BossPhaseComponent
{
    /// @brief 現在の大状態。書き手は BossPhaseSystem のみ。
    BossPhase phase = BossPhase::Combat;

    /// @brief 状態滞在タイマー（秒）。入場時にセットし、毎フレーム減る。
    ///        「咆哮の演出が終わるまで無敵」のような時間で終わる状態に使う。
    float timer = 0.0f;
};
