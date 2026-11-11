#pragma once
// ===========================================================================
// 概要: ゲーム中に勝敗を判定し、リザルト画面をゲーム画面の上に重ねて出す System
//
// 責務:
//   - プレイヤーが倒れたら MISSION FAILED、敵を全部倒したら MISSION COMPLETE を出す
//   - 戦いの記録（時間・命中・被ダメージ・ジャスト回避・群れの数）を数え、リザルトに出す
//   - 決着したら GameFlowState::matchOver を立て、操作とボスを止める
//   - 「もう一度」「タイトルへ」のボタンを受けてシーンを切り替える
//
// 備考: 全体登録はせず、GameScene と EditorScene が登録する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <atomic>

class GameResultSystem : public CCL::ECS::IfSystem<
    GameResultSystem,
    CCL::ECS::Read<HealthComponent>>
{
public:
    GameResultSystem() : IfSystem("GameResultSystem") {}

    void Initialize() override;     // ボタンと戦いの記録のイベントを購読する
    void Update(float dt) override; // 勝敗を判定し、記録を数える
    void OnGui() override;          // 判定の状態の表示と、リザルトを出すボタン（デバッグ用）

private:
    void CountBattleStats(float dt); // 戦っている間の記録（時間・プレイヤーの ID）を数える
    // 決着を GameFlowState に書き、プレイヤーの操作・ボスの思考と技・視点入力をまとめて止める
    void EndMatch();                 // 決着を GameFlowState に書き、操作とボスを止める
    void ShowResultUI(bool win);     // リザルトの UI（暗転・見出し・記録・ボタン）を作る

    bool  m_resultShown   = false; // リザルトを出したか
    bool  m_sawEnemy      = false; // 敵を一度でも見たか
    bool  m_transitioning = false; // シーンの切り替えを頼んだ後か
    bool  m_pendingWin    = false; // 勝ちを判定した後、リザルトを出すのを待っているか
    float m_winDelay      = 0.0f;  // 勝ちのリザルトを出すまでの残り時間（実時間の秒）

    // --- 戦いの記録（イベントで数えるものは、購読がワーカースレッドで走ることがあるので atomic） ---
    float                           m_playTime    = 0.0f;                  // 戦っていた時間（秒。ポーズ中は数えない）
    std::atomic<int>                m_hits{0};                             // プレイヤーの攻撃が当たった回数
    std::atomic<int>                m_damageX10{0};                        // プレイヤーが受けたダメージの合計の 10 倍
    std::atomic<int>                m_justEvades{0};                       // ジャスト回避の成功回数
    std::atomic<CCL::ECS::EntityID> m_playerID{CCL::ECS::InvalidEntityID}; // プレイヤー。命中の攻撃者と比べる
    float                           m_bossHpRatio = 1.0f;                  // 結果が出た時点のボスの HP の割合（0〜1）

    // --- デバッグ用 ---
    bool m_debugEnabled     = true;  // false なら勝敗の判定もリザルトの表示もしない
    bool m_dbgPlayerPresent = false; // 直前のフレームでプレイヤーがいたか
    int  m_dbgAliveEnemies  = 0;     // 直前のフレームで生きていた敵の数

    static constexpr float kResultDelay = 1.0f; // 勝ちを判定してからリザルトを出すまでの最低の待ち時間（実時間の秒）
};
