#pragma once
// タイトル画面のメニューUIを生成し、ボタンクリックでシーン遷移するシステム。
//
// 設計方針:
//   - Initialize でタイトル文字＋START/QUIT ボタンを ECS UI として生成。
//   - UIButtonClickedEvent を購読し、onClickAction のハッシュで遷移を分岐。
//   - グローバル登録（REGISTER_*_SYSTEM）はしない。TitleScene が手動登録する
//     （ゲーム本編でタイトルメニューが湧かないようにするため）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

class TitleMenuSystem
    : public CCL::ECS::IfSystem<TitleMenuSystem, CCL::ECS::Read<UIButtonComponent>>
{
  public:
    TitleMenuSystem() : IfSystem("TitleMenuSystem") {}

    void Initialize() override;      // UI生成＋イベント購読
    void Update(float /*dt*/) override {} // 毎フレーム処理は不要（イベント駆動）

  private:
    bool m_transitioning = false; // 二重遷移防止（連打・多重発火対策）
};
