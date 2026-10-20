#pragma once
// ===========================================================================
// 概要: ボスの形態（第1形態・移行演出・第2形態・死亡）を持つ Component。
//
// 責務:
//   - 現在の形態と、その形態に入ってからのタイマーを持つ
//   - 第2形態の BT のパスと、移行する HP のしきい値を持つ
//
// 備考:
//   形態を決めるのは BossPhaseSystem。その中で技を選ぶのが BT、体の動きに変えるのが BossActionSystem。
//   第2形態の BT のパスとしきい値は BehaviorTreeComponent ではなくここに持つ。
//   BehaviorTreeComponent は汎用のデータなので、ボスにしか無いものを置くと敵を増やすたびにフィールドが増える。
//
//   形態の移行: (Standby <--章--> Intro --教え終えた--> ) Combat --HP<=しきい値--> Transition --演出時間の経過--> Combat2
//   Transition の間は BT を止め、無敵にし、ドローンを頭上へ退避させる。攻撃が途切れて
//   仕切り直す時間ができ、形態が変わったことが画面で分かる。
//   Standby と Intro はチュートリアルシーンの序盤だけ。教えている章（TutorialSteps.h）に合わせて行き来する。
//     Standby: 章1・2。BT を止め、攻撃を出さずにドローンを頭上で回す（移動や攻撃を覚える間は撃たない）
//     Intro  : 章3。予兆の長い攻撃だけを出す BT（introAssetPath）に差し替え、手加減して攻める
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <string>

// 並び順がそのままインスペクタの列挙値になる。増やすときは末尾に足すこと
// （途中に挿すと保存済みプレハブの値がずれる）。
enum class BossPhase : uint8_t
{
    Combat,     // 第1形態。BT（プレハブの assetPath）が技を選ぶ
    Dead,       // 死亡。終端状態。ここからは抜けない
    Transition, // 形態移行の演出中。BT を止め、無敵になり、ドローンを頭上へ退避させる
    Combat2,    // 第2形態。BT を phase2AssetPath のものへ差し替えて戦う
    Intro,      // チュートリアルの序盤（章3）。BT を introAssetPath のものへ差し替え、手加減して戦う
    Standby,    // チュートリアルの序盤（章1・2）。BT を止め、攻撃を出さずに待つ
};

// 表示名。名前の二重管理を避けるため enum の隣に置く。
inline constexpr const char *kBossPhaseNames[] = {
    "Combat",
    "Dead",
    "Transition",
    "Combat2",
    "Intro",
    "Standby",
};
inline constexpr size_t kBossPhaseCount = sizeof(kBossPhaseNames) / sizeof(kBossPhaseNames[0]);

inline const char *ToString(BossPhase p)
{
    const size_t i = static_cast<size_t>(p);
    return (i < kBossPhaseCount) ? kBossPhaseNames[i] : "?";
}

struct BossPhaseComponent
{
    // 第2形態で使う BT のパス。空なら形態移行をしない（設定し忘れても BT が止まらないようにする）。
    std::string phase2AssetPath;

    // 第2形態へ移行する HP の割合（0〜100）。
    float phase2HealthThreshold = 50.0f;

    // チュートリアルの序盤（章3）で使う BT のパス。空なら序盤（待機も含む）を設けず、最初から本来の BT で戦う。
    std::string introAssetPath;

    // 現在の形態。書くのは BossPhaseSystem だけ。
    BossPhase phase = BossPhase::Combat;

    float timer = 0.0f; // 形態の残り時間（秒）。形態に入ったときに設定し、毎フレーム減らす（移行演出で使う）

    // --- 序盤の実行時の状態（保存しない） ---
    std::string combatAssetPath; // 序盤（待機・手加減）に入る前の BT のパス。序盤を終えるときに戻す
};
