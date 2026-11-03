#pragma once
// ===========================================================================
// 概要: チュートリアルを教え終えたら、完了の画面を出して次へ進ませる System。
//
// 責務:
//   - 教え終えたら、GameFlowState::matchOver を立てて操作とボスを止める
//   - 少し待ってから、完了の画面をゲーム画面の上に重ねて出す
//   - 「本番へ」「もう一度」「タイトルへ」のボタンを受けてシーンを切り替える
//
// 備考: 全体登録はせず、TutorialScene が登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include <vector>

class TutorialCompleteSystem : public CCL::ECS::IfSystem<TutorialCompleteSystem>
{
public:
    TutorialCompleteSystem() : IfSystem("TutorialCompleteSystem") {}

    void Initialize() override;     // ボタンのイベントを購読する
    void Update(float dt) override; // 教え終えたかを見て、画面を出す・消す

private:
    void ShowUI();  // 完了の画面（暗転・見出し・ボタン）を作る
    void CloseUI(); // 完了の画面を消し、操作とボスを戻す（デバッグでやり直したとき）

    // matchOver を書く。決着と同じく、プレイヤーの操作・ボスの思考と技・視点入力をまとめて止める
    void SetMatchOver(bool over);

    bool  m_done          = false; // 教え終えたのを見たか
    bool  m_shown         = false; // 画面を出したか
    float m_showDelay     = 0.0f;  // 画面を出すまでの残り時間（実時間の秒）
    bool  m_transitioning = false; // シーンの切り替えを頼んだ後か

    std::vector<CCL::ECS::EntityID> m_overlay; // 作った UI（CloseUI でまとめて消す）
};
