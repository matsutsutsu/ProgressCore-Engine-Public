#pragma once
// ===========================================================================
// 概要: チュートリアルの進み具合（World リソース TutorialState）。
//
// 責務:
//   - いま教えている項目と、ヒントを画面に出すかを System 間で共有する
//
// 備考: TutorialState の書き手は TutorialSystem だけ。読むのは BossPhaseSystem（序盤の BT を使うか）と
//       TutorialHintHudSystem（画面のヒント）、TutorialCompleteSystem（完了の画面）。
//       TutorialSystem は TutorialScene だけが登録するので、このリソースがあるのはチュートリアルシーンだけ。
//       ゲーム本編（GameScene）には無く、BossPhaseSystem は序盤（手加減）を設けずに最初から本来の BT で戦う。
//       教える項目の中身は TutorialSteps.h の表にある。
// ===========================================================================
#include "Game/Logics/Tutorial/TutorialSteps.h"

#include <cstdint>

struct TutorialState
{
    uint8_t step         = 0;     // いま教えている項目（kTutorialSteps の添字）。kTutorialStepCount なら教え終えた
    bool    showHint     = false; // ヒントを画面に出すか。項目の途中でも、出す場面を選ぶため別に持つ
    bool    assistActive = false; // 回避の補助スロー中か。ヒントの文言を assistText に差し替える

    // 序盤（ボスが手加減する時間）を終えてよいか。BossPhaseSystem が見る
    bool IsFinished() const { return step >= kTutorialStepCount; }

    // いま教えている項目。教え終えたら nullptr
    const TutorialStepDef *Current() const { return IsFinished() ? nullptr : &kTutorialSteps[step]; }
};
