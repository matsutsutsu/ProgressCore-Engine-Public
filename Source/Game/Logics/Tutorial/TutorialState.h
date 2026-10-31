#pragma once
// ===========================================================================
// 概要: チュートリアルの進み具合（World リソース TutorialState）。
//
// 責務:
//   - いま教えている項目と、ヒントを画面に出すかを System 間で共有する
//
// 備考: TutorialState の書き手は TutorialSystem だけ。読むのは BossPhaseSystem（ボスの待機と手加減）と
//       TutorialHintHudSystem（画面のヒント）、TutorialCompleteSystem（完了の画面）。
//       TutorialSystem は TutorialScene だけが登録するので、このリソースがあるのはチュートリアルシーンだけ。
//       ゲーム本編（GameScene）には無く、BossPhaseSystem はチュートリアルの待機と手加減に入らず、最初から本来の BT で戦う。
//       教える項目の中身は TutorialSteps.h の表にある。
// ===========================================================================
#include "Game/Logics/Tutorial/TutorialSteps.h"

#include <cstdint>

struct TutorialState
{
    uint8_t step         = 0;     // いま教えている項目（kTutorialSteps の添字）。kTutorialStepCount なら教え終えた
    bool    showHint     = false; // ヒントを画面に出すか
    bool    assistActive = false; // 回避の補助スローで「今だ」を出す段階か（ヒントを assistText に差し替える）

    // --- 進み具合（ヒントの帯をゲージにして出す） ---
    float progress = 0.0f;  // 今の項目の達成までの割合（0〜1）
    bool  cleared  = false; // 達成して、次の項目へ進むまでの余韻の最中か（帯を満たしたまま待つ）

    // 教え終えたか
    bool IsFinished() const { return step >= kTutorialStepCount; }

    // いま教えている項目。教え終えたら nullptr
    const TutorialStepDef *Current() const { return IsFinished() ? nullptr : &kTutorialSteps[step]; }

    // ボスを待たせるか（攻撃を出させない）。いま教えている項目の bossAttacks で決まる。BossPhaseSystem が見る。
    // 教え終えた後も待たせる。完了の画面の裏で、ボスが攻撃を始めないようにするため
    bool IsBossWaiting() const { return IsFinished() || !kTutorialSteps[step].bossAttacks; }
};
