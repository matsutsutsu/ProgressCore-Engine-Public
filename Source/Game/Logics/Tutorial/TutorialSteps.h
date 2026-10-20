#pragma once
// ===========================================================================
// 概要: チュートリアルで教える項目の表（kTutorialSteps）。
//
// 責務:
//   - 項目ごとに、ヒントのアイコンと文言・達成の条件・時間切れを持つ
//   - 並び順で教える順番を決める
//
// 備考: 教える中身を変えるときは、この表だけを書き換える。
//       達成の条件の判定（何をしたら達成か）は TutorialSystem が TutorialGoal ごとに持つ。
//       条件の種類を増やしたら、TutorialSystem::Update の switch にも足すこと。
// ===========================================================================
#include <cstdint>
#include <iterator> // std::size

// 達成の条件の種類。項目ごとに、何をしたら次へ進むかを表す
enum class TutorialGoal : uint8_t
{
    Wait,      // 条件なし。時間切れ（timeout）だけで進む
    JustEvade, // ジャスト回避を成立させる。陣形が組まれ始めてからヒントを出し、突撃の直前に補助スローを掛ける
    AttackHit, // プレイヤーの攻撃を当てる
};

// 教える項目1つぶん
struct TutorialStepDef
{
    const char  *name;       // 名前（デバッグ表示用）
    const char  *action;     // ヒントのアイコンにするアクション名（InputConfig.json の Name）。nullptr ならヒントを出さない
    const char  *text;       // ヒントの1行（UTF-8）
    const char  *assistText; // 補助スロー中に差し替える1行。nullptr なら差し替えない
    TutorialGoal goal;       // 達成の条件
    float        timeout;    // 達成しなくても、この秒数（実時間）で次へ進む。0 なら達成するまで待つ
};

// 教える項目の表。並び順がそのまま教える順番になる
inline constexpr TutorialStepDef kTutorialSteps[] = {
    // シーンに入ってすぐは明転の途中なので、少し待ってから始める
    {"WaitBattle", nullptr, "", nullptr, TutorialGoal::Wait, 1.0f},

    // 回避: 形を見る前に「回避」と言っても、何を避けるのか分からないので、陣形が組まれ始めてからヒントを出す
    {"ReadAndEvade", "Dash", "陣形の形を読み、突っ込んでくる瞬間に回避", "今だ！ 回避", TutorialGoal::JustEvade, 0.0f},

    // 攻撃: ジャスト回避の接近でボスの近くへ運ばれている間に促す。当てなくても時間で次へ進める
    // （ボスに背を向けて回避した、などで届かないことがあり、そこで止めると先を教えられない）
    {"Strike", "Attack", "ジャスト回避でボスの懐へ。そのまま攻撃", nullptr, TutorialGoal::AttackHit, 5.0f},
};

// 項目の数。TutorialState.step がこれに達したら教え終えた
inline constexpr uint8_t kTutorialStepCount = static_cast<uint8_t>(std::size(kTutorialSteps));
