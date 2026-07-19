#pragma once
/// @file GameResultSystem.h
/// @brief ゲームシーン内で勝敗を検知し、リザルトUIを「重ねて」表示するシステム。
///
/// 設計方針:
///   - リザルトは専用シーンに遷移せず、ゲーム世界の中に UI オーバーレイとして出す。
///   - 敗北：プレイヤー（PlayerComponent を持つ HealthComponent）が 0 以下 → GAME OVER（即表示）。
///   - 勝利：敵（PlayerComponent を持たない HealthComponent）を一度観測した後、
///           生存している敵が 0 になったら → GAME CLEAR。
///           （開幕でまだ敵が湧いていない 0 体状態での即勝利を防ぐため観測フラグを持つ）
///   - ★勝利は即表示しない：最後の敵撃破と同じフレームで DefeatSlowMotionSystem の撃破スローが
///     始まるため、その余韻(実時間 kResultDelay 秒)を見せてからリザルトを出す。Update に渡る dt は
///     スロー非依存の実時間なので、実時間で待てば「スローが終わってからリザルト」になる。
///   - ボタンは UIButtonClickedEvent を購読して RETRY=ゲーム再読込 / TITLE=タイトルへ。
///   - グローバル登録はしない（EditorScene が手動登録＝ゲーム本編でだけ動かす）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"

class GameResultSystem
    : public CCL::ECS::IfSystem<GameResultSystem, CCL::ECS::Read<HealthComponent>>
{
  public:
    GameResultSystem() : IfSystem("GameResultSystem") {}

    void Initialize() override; ///< ボタンイベント購読
    void Update(float dt) override; ///< 勝敗検知

  private:
    void ShowResultUI(bool win); ///< リザルトUI（暗転＋文字＋ボタン）を生成

    bool  _resultShown   = false; ///< 一度出したら維持（多重生成防止）
    bool  _sawEnemy      = false; ///< 敵を一度でも観測したか（開幕0体での即勝利防止）
    bool  _transitioning = false; ///< 遷移予約後の連打無視
    bool  _pendingWin    = false; ///< 勝利検知後、撃破スローの余韻を見せている待機中か
    float _winDelay      = 0.0f;  ///< 勝利リザルト表示までの残り時間（秒・実時間）

    /// 勝利検知からリザルト表示までの待ち時間(秒)。DefeatSlowMotionSystem の
    /// クライマックススロー(kClimaxDur≈0.85s)より少し長くして、スローが完全に戻ってから出す。
    static constexpr float kResultDelay = 1.0f;
};
