#pragma once
// ===========================================================================
// 概要: ゲーム中に勝敗を判定し、リザルト画面の UI をゲーム画面の上に重ねて出す System。
//
// 責務:
//   - プレイヤーが倒れたら MISSION FAILED を出す
//   - 敵を一度でも見た後、生きている敵が 0 になったら、少し待って MISSION COMPLETE を出す
//   - 戦いの記録（時間・命中・被ダメージ・ジャスト回避・群れの数）を数え、リザルトに1行ずつ打ち込む
//   - 決着したら GameFlowState::matchOver を立て、操作とボスを止める
//   - 「もう一度」（ゲームを読み直す）と「タイトルへ」のボタンを受けてシーンを切り替える
//
// 備考:
//   プレイヤーは HealthComponent と PlayerComponent を持つもの、敵は HealthComponent と EnemyTag を持つものとして数える。
//
//   画面の構成はローディング画面にそろえる（左上の大きな見出し・線・記録の行・その下にボタン）。
//   組みはポーズと共通なので ScreenHeadingUi.h で作り、出てくる演出は UIRevealSystem が動かす。
//
//   全体登録（REGISTER_*）はせず、GameScene と EditorScene が登録する。ゲーム本編でだけ動かす。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <atomic>

class GameResultSystem
    : public CCL::ECS::IfSystem<GameResultSystem, CCL::ECS::Read<HealthComponent>>
{
public:
    GameResultSystem() : IfSystem("GameResultSystem") {}

    void Initialize() override;     // ボタンと戦いの記録のイベントを購読する
    void Update(float dt) override; // 勝敗を判定し、記録を数える
    void OnGui() override;          // 判定の状態を表示し、判定を止められるようにする（デバッグ用）

private:
    void CountBattleStats(float dt); // 戦っている間の記録（時間・群れの数）を数える
    void EndMatch();                 // 決着を GameFlowState に書き、操作とボスを止める
    void ShowResultUI(bool win);     // リザルトの UI（暗転・見出し・記録・ボタン）を作る

    bool  m_resultShown   = false; // リザルトを出したか
    bool  m_sawEnemy      = false; // 敵を一度でも見たか
    bool  m_transitioning = false; // シーンの切り替えを頼んだ後か
    bool  m_pendingWin    = false; // 勝ちを判定した後、リザルトを出すのを待っているか
    float m_winDelay      = 0.0f;  // 勝ちのリザルトを出すまでの残り時間（実時間の秒）

    // --- 戦いの記録（イベントで数えるものは、購読がワーカースレッドで走ることがあるので atomic） ---
    float                           m_playTime    = 0.0f;                     // 戦っていた時間（秒。ポーズ中は数えない）
    std::atomic<int>                m_hits{0};                                // プレイヤーの攻撃が当たった回数
    std::atomic<int>                m_damageX10{0};                           // プレイヤーが受けたダメージの合計の 10 倍（整数で足すため）
    std::atomic<int>                m_justEvades{0};                          // ジャスト回避の成功回数
    std::atomic<CCL::ECS::EntityID> m_playerID{CCL::ECS::InvalidEntityID};    // プレイヤー。命中の攻撃者と比べる
    int                             m_swarmMax    = 0;                        // 戦った群れの最大機数
    float                           m_bossHpRatio = 1.0f;                     // 結果が出た時点のボスの HP の割合（0〜1）

    // --- デバッグ用 ---
    bool m_debugEnabled     = true;  // false なら勝敗の判定もリザルトの表示もしない
    bool m_dbgPlayerPresent = false; // 直前のフレームでプレイヤーが居たか（表示用）
    int  m_dbgAliveEnemies  = 0;     // 直前のフレームで生きていた敵の数（表示用）

    static constexpr float kResultDelay = 1.0f; // 勝ちを判定してからリザルトを出すまでの最低の待ち時間（実時間の秒）
};
