#pragma once
// ===========================================================================
// 概要: ゲーム中に勝敗を判定し、リザルト画面の UI をゲーム画面の上に重ねて出す System。
//
// 責務:
//   - プレイヤーが倒れたら MISSION FAILED を出す
//   - 敵を一度でも見た後、生きている敵が 0 になったら、少し待って MISSION COMPLETE を出す
//   - 戦いの記録（時間・命中・被ダメージ・ジャスト回避・切り払い・群れの数）を数え、リザルトに1行ずつ打ち込む
//   - RETRY（ゲームを読み直す）と TITLE（タイトルへ戻る）のボタンを受けてシーンを切り替える
//
// 備考: 敵を一度見たかを持つのは、始まった直後のまだ敵が出ていない状態で勝ちにしないため。
//       勝ちをすぐ出さないのは、最後の敵を倒したフレームから DefeatSlowMotionSystem のスローが
//       始まるため。Update の dt はスローの影響を受けない実時間なので、kResultDelay 秒待てば
//       スローが終わってから出せる。
//       画面の構成はローディング画面にそろえる（左上の大きな見出し・線・記録の行・その下にボタン）。
//       記録のうちイベントで数えるもの（命中・被ダメージ・ジャスト回避）は、購読がワーカースレッドで
//       走ることがあるので atomic にする。
//       プレイヤーは HealthComponent と PlayerComponent を持つもの、敵は HealthComponent と EnemyTag を持つもの。
//       全体登録（REGISTER_*）はせず、GameScene と EditorScene が登録する（ゲーム本編でだけ動かす）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h" // CleavePhase（切り払いの回数を数える）

#include <DirectXMath.h>
#include <atomic>
#include <string>
#include <vector>

class GameResultSystem
    : public CCL::ECS::IfSystem<GameResultSystem, CCL::ECS::Read<HealthComponent>>
{
public:
    GameResultSystem() : IfSystem("GameResultSystem")
    {
    }

    void Initialize() override;     // ボタンと戦いの記録のイベントを購読する
    void Update(float dt) override; // 勝敗を判定し、記録を数え、リザルトの演出を進める
    void OnGui() override;          // 判定の状態を表示し、判定を止められるようにする（デバッグ用）

private:
    // 1文字ずつ打ち込む文字列。start 秒になったら打ち始める
    struct TypedText
    {
        CCL::ECS::EntityID id = CCL::ECS::InvalidEntityID;
        std::string        full;          // 打ち終わったときの文字列
        float              start = 0.0f;  // 打ち始める時刻（リザルトを出してからの秒）
    };

    void CountBattleStats(float dt); // 戦っている間の記録（時間・切り払い・群れの数）を数える
    void ShowResultUI(bool win);     // リザルトの UI（暗転・見出し・記録・ボタン）を作る
    void AnimateResult(float dt);    // 見出しの滑り込み・線の伸び・記録の打ち込みを進める

    // 画面の左上を基準に、左上 (x, y) から文字を置く。alpha は最初の不透明度（演出で出すものは 0 で作る）
    CCL::ECS::EntityID SpawnText(const char *name, const std::string &text, float x, float y, float px,
                                 DirectX::XMFLOAT4 color, CCL::ECS::EntityID parent, float alpha = 1.0f);
    // 画面の左上を基準に、中心 (x, y)・大きさ size・回転 rotation（度）の単色の矩形を置く
    CCL::ECS::EntityID SpawnRect(const char *name, float x, float y, DirectX::XMFLOAT2 size, float rotation,
                                 DirectX::XMFLOAT4 color, CCL::ECS::EntityID parent, float alpha = 1.0f);

    bool  m_resultShown   = false; // リザルトを出したか（2回作らないため）
    bool  m_sawEnemy      = false; // 敵を一度でも見たか（始まった直後の敵 0 体で勝ちにしないため）
    bool  m_transitioning = false; // シーンの切り替えを頼んだ後か（連打を無視するため）
    bool  m_pendingWin    = false; // 勝ちを判定した後、スローが終わるのを待っているか
    float m_winDelay      = 0.0f;  // 勝ちのリザルトを出すまでの残り時間（実時間の秒）

    // --- 戦いの記録 ---
    float                           m_playTime   = 0.0f; // 戦っていた時間（秒。ポーズ中は数えない）
    std::atomic<int>                m_hits{0};           // プレイヤーの攻撃が当たった回数
    std::atomic<int>                m_damageX10{0};      // プレイヤーが受けたダメージの合計の 10 倍（atomic<float> を避けて整数で足す）
    std::atomic<int>                m_justEvades{0};     // ジャスト回避の成功回数
    std::atomic<CCL::ECS::EntityID> m_playerID{CCL::ECS::InvalidEntityID}; // 命中を数えるときに攻撃した側と比べる
    int                             m_cleaves      = 0; // 切り払いの突進の回数
    int                             m_maxChain     = 0; // 切り払いの最大チェイン段数
    int                             m_swarmMax     = 0; // 戦った群れの最大機数
    float                           m_bossHpRatio  = 1.0f; // 結果が出た時点のボスの HP の割合（負けのときに出す）
    CleavePhase                     m_prevCleave   = CleavePhase::Idle; // 前フレームの切り払いのフェーズ（突進に入った瞬間を数える）

    // --- リザルトの演出 ---
    float                   m_resultTime = 0.0f;                     // リザルトを出してからの秒数
    CCL::ECS::EntityID      m_headingID  = CCL::ECS::InvalidEntityID; // 大きな見出し（左から滑り込む）
    CCL::ECS::EntityID      m_ruleID     = CCL::ECS::InvalidEntityID; // 見出しの下の線（左から伸びる）
    CCL::ECS::EntityID      m_ruleTipID  = CCL::ECS::InvalidEntityID; // 線の右端の◆（線の先端についていく）
    std::vector<TypedText>  m_typed;                                  // 打ち込む文字列（行き先の行・記録の行）

    // --- デバッグ用 ---
    bool m_debugEnabled     = true;  // false なら勝敗の判定もリザルトの表示もしない
    bool m_dbgPlayerPresent = false; // 直前のフレームでプレイヤーが居たか（表示用）
    int  m_dbgAliveEnemies  = 0;     // 直前のフレームで生きていた敵の数（表示用）

    // 勝ちを判定してからリザルトを出すまでの時間（秒）。DefeatSlowMotionSystem の
    // ボス撃破のスロー（kClimaxDur = 0.85 秒）より少し長くして、スローが戻ってから出す
    static constexpr float kResultDelay = 1.0f;
};
