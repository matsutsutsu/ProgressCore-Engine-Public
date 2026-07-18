#pragma once
/// @file PauseSystem.h
/// @brief ESC / パッドSTART でゲームをポーズし、ポーズメニュー（RESUME/TITLE）を重ねて表示する。
///
/// 設計方針:
///   - 描画ステージ(R10_UI)に登録＝ポーズ中も毎フレーム動く（ポーズは FixedUpdate を止めるため）。
///   - トグルで GameFlowState.paused を切り替える。BaseScene がこれを見て
///     ロジック/物理を止め、描画は UpdatePaused で継続する。
///   - オーバーレイは開時に生成、解除時に破棄（生成した実体IDを保持しておく）。
///   - グローバル登録はしない（EditorScene が手動登録＝ゲーム本編でだけ動かす）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

#include <vector>

class PauseSystem
    : public CCL::ECS::IfSystem<PauseSystem, CCL::ECS::Read<UIButtonComponent>>
{
  public:
    PauseSystem() : IfSystem("PauseSystem") {}

    void Initialize() override; ///< GameFlowState登録＋ボタンイベント購読
    void Update(float dt) override; ///< トグル入力の検知

  private:
    void OpenPause();  ///< ポーズON：オーバーレイ生成
    void ClosePause(); ///< ポーズOFF：オーバーレイ破棄

    bool _paused        = false;
    bool _transitioning = false; ///< TITLE遷移予約後の連打無視
    std::vector<CCL::ECS::EntityID> _overlay; ///< 生成したオーバーレイ実体（解除時に破棄）
};
