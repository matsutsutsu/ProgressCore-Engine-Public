#pragma once
// ===========================================================================
// 概要: チュートリアルの進み具合（World リソース TutorialState）と、
//       シーンをまたいで残る「この遊び手はもう教わったか」の記録（TutorialSession）。
//
// 責務:
//   - いま教えている項目と、ヒントを画面に出すかを System 間で共有する
//   - この挑戦でチュートリアルを行うかを持つ
//   - リトライで同じ説明を繰り返さないよう、教え終えたことをアプリ全体で覚えておく
//
// 備考: TutorialState の書き手は TutorialSystem だけ。読むのは BossPhaseSystem（序盤の BT を使うか）と
//       TutorialHintHudSystem（画面のヒント）。
//       チュートリアルを行うのはタイトル画面を通った挑戦だけ（TutorialSession.armed）。
//       エディタシーンから直接遊ぶときは出ない。エディタで確かめたいときは TutorialSystem の
//       調整窓（OnGui）から手動で始める。
//       展示会では遊び手がタイトルから入れ替わるので、タイトルに入るたびに記録を消し、次の人にまた教える。
// ===========================================================================
#include <cstdint>

// 教える項目。並び順がそのまま教える順番になる
enum class TutorialStep : uint8_t
{
    WaitBattle,   // 戦闘の開始（開幕ムービーの終わり）を待つ
    ReadAndEvade, // 陣形の形を読み、突撃に合わせてジャスト回避する
    Strike,       // ジャスト回避の接近から、ボスへ攻撃を当てる
    Done,         // 教え終えた
};

// 表示名（デバッグ表示用）。名前の二重管理を避けるため enum の隣に置く
inline constexpr const char *kTutorialStepNames[] = {
    "WaitBattle",
    "ReadAndEvade",
    "Strike",
    "Done",
};

struct TutorialState
{
    bool         enabled      = false;                    // この挑戦でチュートリアルを行うか
    TutorialStep step         = TutorialStep::WaitBattle; // いま教えている項目
    bool         showHint     = false;                    // ヒントを画面に出すか。項目の途中でも、出す場面を選ぶため別に持つ
    bool         assistActive = false;                    // 回避の補助スロー中か。ヒントの文言を「今！」に変える

    // チュートリアルを始めた回数。始めるたびに増やす。
    // BossPhaseSystem がこれの変化を見て、序盤（Intro）に入り直す（エディタから最初からやり直したとき）
    uint32_t generation = 0;

    // 序盤（ボスが手加減する時間）を終えてよいか。BossPhaseSystem が見る
    bool IsFinished() const { return !enabled || step == TutorialStep::Done; }
};

// シーンをまたいで残る記録。World はシーンごとに作り直されるので、アプリ全体で1つ持つ
// ※多分後でタイトルからチュートリアル受けるかどうかで選択させるから消すかも
struct TutorialSession
{
    bool armed     = false; // 次の挑戦でチュートリアルを行うか。タイトル画面に入ると立つ（エディタシーンからは立たない）
    bool completed = false; // この遊び手が教わり終えたか

    static TutorialSession &Instance()
    {
        static TutorialSession s;
        return s;
    }
};
