#pragma once
// ===========================================================================
// 概要: ゲーム中に勝敗を判定し、リザルト画面の UI をゲーム画面の上に重ねて出す System。
//
// 責務:
//   - プレイヤーが倒れたら GAME OVER を出す
//   - 敵を一度でも見た後、生きている敵が 0 になったら、少し待って GAME CLEAR を出す
//   - RETRY（ゲームを読み直す）と TITLE（タイトルへ戻る）のボタンを受けてシーンを切り替える
//
// 備考: 敵を一度見たかを持つのは、始まった直後のまだ敵が出ていない状態で勝ちにしないため。
//       勝ちをすぐ出さないのは、最後の敵を倒したフレームから DefeatSlowMotionSystem のスローが
//       始まるため。Update の dt はスローの影響を受けない実時間なので、kResultDelay 秒待てば
//       スローが終わってから出せる。
//       プレイヤーは HealthComponent と PlayerComponent を持つもの、敵は PlayerComponent を持たないもの。
//       全体登録（REGISTER_*）はせず、GameScene と EditorScene が登録する（ゲーム本編でだけ動かす）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"

class GameResultSystem
    : public CCL::ECS::IfSystem<GameResultSystem, CCL::ECS::Read<HealthComponent>>
{
public:
    GameResultSystem() : IfSystem("GameResultSystem")
    {
    }

    void Initialize() override;     // ボタンのイベントを購読する
    void Update(float dt) override; // 勝敗を判定する
    void OnGui() override;          // 判定の状態を表示し、判定を止められるようにする（デバッグ用）

private:
    void ShowResultUI(bool win); // リザルトの UI（暗転・見出し・ボタン）を作る

    bool  m_resultShown   = false; // リザルトを出したか（2回作らないため）
    bool  m_sawEnemy      = false; // 敵を一度でも見たか（始まった直後の敵 0 体で勝ちにしないため）
    bool  m_transitioning = false; // シーンの切り替えを頼んだ後か（連打を無視するため）
    bool  m_pendingWin    = false; // 勝ちを判定した後、スローが終わるのを待っているか
    float m_winDelay      = 0.0f;  // 勝ちのリザルトを出すまでの残り時間（実時間の秒）

    // --- デバッグ用 ---
    bool m_debugEnabled     = true;  // false なら勝敗の判定もリザルトの表示もしない
    bool m_dbgPlayerPresent = false; // 直前のフレームでプレイヤーが居たか（表示用）
    int  m_dbgAliveEnemies  = 0;     // 直前のフレームで生きていた敵の数（表示用）

    // 勝ちを判定してからリザルトを出すまでの時間（秒）。DefeatSlowMotionSystem の
    // ボス撃破のスロー（kClimaxDur = 0.85 秒）より少し長くして、スローが戻ってから出す
    static constexpr float kResultDelay = 1.0f;
};
