#pragma once
/// @file GameResultSystem.h
/// @brief ゲームシーン内でゲームオーバーを検知し、リザルトUIを「重ねて」表示するシステム。
///
/// 設計方針:
///   - リザルトは専用シーンに遷移せず、ゲーム世界の中に UI オーバーレイとして出す。
///   - プレイヤー（PlayerComponent）の HealthComponent が 0 以下になったらゲームオーバー。
///   - 検知したら暗転パネル＋GAME OVER＋RETRY/TITLE ボタンを生成。
///   - ボタンは UIButtonClickedEvent を購読して RETRY=ゲーム再読込 / TITLE=タイトルへ。
///   - グローバル登録はしない（EditorScene が手動登録＝ゲーム本編でだけ動かす）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"

class GameResultSystem
    : public CCL::ECS::IfSystem<GameResultSystem,
          CCL::ECS::Read<PlayerComponent>, CCL::ECS::Read<HealthComponent>>
{
  public:
    GameResultSystem() : IfSystem("GameResultSystem") {}

    void Initialize() override; ///< ボタンイベント購読
    void Update(float dt) override; ///< ゲームオーバー検知

  private:
    void ShowResultUI(); ///< リザルトUI（暗転＋文字＋ボタン）を生成

    bool _gameOver      = false; ///< 一度立ったら維持（多重生成防止）
    bool _transitioning = false; ///< 遷移予約後の連打無視
};
