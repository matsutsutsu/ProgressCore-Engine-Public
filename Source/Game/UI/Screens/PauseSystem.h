#pragma once
// ===========================================================================
// 概要: Esc かパッドの START でゲームを一時停止し、ポーズメニュー（ゲームに戻る / 設定 / タイトルへ）を重ねて出す System。
//
// 責務:
//   - 一時停止の切り替えを受け、GameFlowState.paused とゲームの時間（Time::Pause / Resume）を切り替える
//   - 一時停止したらメニューの UI（左上の見出し・ボタン）を作り、解除したら消す
//   - 「ゲームに戻る」と「タイトルへ」のボタンを受ける（「設定」は SettingsScreenSystem が受ける）
//   - 製品ビルドでは、アプリが裏に回ったら自動で一時停止する
//
// 備考:
//   描画ステージ（R10_UI）に登録する。
//   一時停止中はロジックと物理が止まり、描画だけが UpdatePaused で回るので、ロジックステージだと解除の入力を拾えない。
//
//   画面の組みはリザルトと共通なので、ScreenHeadingUi.h で作る。
//
//   全体登録（REGISTER_*）はせず、GameScene と EditorScene が登録する。ゲーム本編でだけ動かす。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

#include <vector>

class PauseSystem
    : public CCL::ECS::IfSystem<PauseSystem, CCL::ECS::Read<UIButtonComponent>>
{
public:
    PauseSystem() : IfSystem("PauseSystem") {}

    void Initialize() override;     // GameFlowState を用意し、ボタンのイベントを購読する
    void Update(float dt) override; // 一時停止の切り替えの入力を見る

private:
    void OpenPause();  // 一時停止してメニューを作る
    void ClosePause(); // 再開してメニューを消す

    bool                            m_paused        = false; // 一時停止中か
    bool                            m_transitioning = false; // タイトルへの切り替えを頼んだ後か
    std::vector<CCL::ECS::EntityID> m_overlay;               // 作ったメニューのエンティティ（解除するときに消す）
};
