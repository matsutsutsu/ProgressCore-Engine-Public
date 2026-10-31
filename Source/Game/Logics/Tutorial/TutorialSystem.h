#pragma once
// ===========================================================================
// 概要: チュートリアルシーンで、操作を1つずつ教える System。
//
// 責務:
//   - プレイヤーの行動（移動・視点・ダッシュ・ジャンプ・ロックオン・命中・出た攻撃・ジャスト回避）を見て、
//     項目の達成の条件（TutorialGoal）を判定し、次の項目へ進める
//   - いまヒントを画面に出すべき場面かを決める（陣形が組まれ始めてから回避を促す、など）
//   - 回避を教えている間、突撃が当たる直前に世界を強くスローにして、押す瞬間を教える
//
// 備考: 何を見せるか（文言・順番）は TutorialSteps.h の表が持つ。
//       何をどれだけしたら達成か（距離・回数・時間切れ）は、ここ（cpp の先頭の定数と Update の switch）が持つ。
//       ヒントの見た目は TutorialHintHudSystem、ボスの待機と手加減は BossPhaseSystem が受け持つ。
//       ここは TutorialState を書くだけで、UI もボスも直接は触らない。
//       全体登録はせず、TutorialScene だけが登録する。ゲーム本編（GameScene）では動かない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Tutorial/TutorialState.h"
#include "Game/Logics/AI/Battle/BattleContext.h" // プレイヤーの位置と EntityID

#include <SimpleMath.h>
#include <string>
#include <unordered_map>
#include <vector>

class TutorialSystem : public CCL::ECS::IfSystem<TutorialSystem>
{
public:
    TutorialSystem() : IfSystem("TutorialSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    // デバッグ用の表示と操作（今の項目・進み具合・補助の回数・最初から開始・次の項目へ・完了扱いで終了）
    void OnGui() override;

    // ドローン・ボスの指示・カメラ・プレイヤー・アニメのステートを ForEach を使わずに引くので、ここで申告する
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    /**
     * @brief 回避を教えている間の補助スローを進める。
     *
     * 2段階で掛ける。
     *   予告: 当たるまで kAssistWarnTime 秒になったら、世界を kAssistWarnScale まで遅くする（迫ってくるのを見せる）
     *   今だ: 当たるまで kAssistNowTime 秒になったら、ほぼ止める（kAssistNowScale）。ここで「今だ！」を出す
     * 押すべき瞬間を体で覚えてもらうためで、回数は kMaxAssists までに限る（ずっと助けると覚えないため）。
     *
     * @param state  ヒントの文言を切り替えるため assistActive を書く。
     * @param realDt 実時間の経過（秒）。スロー中も同じ秒数で切れるよう、スローの掛かっていない値を渡す。
     */
    void TickEvadeAssist(TutorialState &state, float realDt);

    /**
     * @brief 突撃中のドローンの位置を記録し、前のフレームとの差から実際の速度を求める。
     *
     * DroneComponent.currentVelocity は使えない。縦割り・横なぎ（RigidFollow）では位置を持ち場へ
     * 直接合わせていて、currentVelocity は実際の速さの 35%（上限あり）しか入っていないため。
     * 突撃をやめた機体の記録はここで消す。
     */
    void SampleDroneMotion();

    /**
     * @brief 突撃中のドローンのうち、最も早くプレイヤーに当たるものまでの残り時間を求める。
     *
     * SampleDroneMotion で求めた速度のまま進んだときに最も近づく時刻と距離を求め、
     * 距離が kAssistMissRadius 以内の機体だけを「当たる」とみなす。
     *
     * @return 当たるまでの残り時間（秒・ゲーム時間）。当たる機体が無ければ負の値。
     */
    float TimeToStrike() const;

    // 補助スローを終え、すぐ等速に戻す
    void EndAssist();

    // 次の項目（kTutorialSteps の添字）へ進め、項目内のタイマーと記録を消す。kTutorialStepCount なら教え終える
    void Advance(TutorialState &state, uint8_t next);

    // ヒントの帯に出す進み具合（done / goal、0〜1 に収める）を書く
    void WriteProgress(TutorialState &state, float done, float goal) const;

    /**
     * @brief チュートリアルを最初から始める（デバッグ用。OnGui から呼ぶ）。
     *
     * 最初の項目はボスを待たせる項目なので、BossPhaseSystem がボスを TutorialWait にする。
     * 補助スローの回数も戻す。
     *
     * @param state 書き換える進み具合。
     */
    void Restart(TutorialState &state);

    // ボスがいま攻撃の陣形を組んでいるか（組み始め〜撃ち終わり）。回避のヒントを出す場面
    bool IsBossAttackFormationActive() const;

    /**
     * @brief 移動の項目で、プレイヤーがこの項目に入ってから水平に動いた距離を足していく。
     * @param battle プレイヤーの足元の位置を引く。
     * @return この項目に入ってから動いた距離（m）。
     */
    float AccumulateMove(const BattleContext &battle);

    /**
     * @brief 視点の項目で、プレイヤーを映すカメラをこの項目に入ってから左右に回した角度を足していく。
     * @param playerID このプレイヤーを映している TPS カメラを探す。
     * @return この項目に入ってから回した角度（度）。往復した分も足す。
     */
    float AccumulateLook(CCL::ECS::EntityID playerID);

    /**
     * @brief プレイヤーのアニメが、このフレームに入ったステートの名前を返す（コンボ・ダッシュ攻撃の項目）。
     *
     * どの攻撃が出るかはアニメグラフが決めるので（PlayerAttackSystem はトリガーを1つ出すだけ）、
     * 「コンボの2段目が出た」「ダッシュ攻撃が出た」はグラフのステートで見る。
     *
     * @param playerID アニメのステートを見るプレイヤー。
     * @return このフレームに切り替わった先のステートの名前（アニメグラフ上の名前）。切り替わっていなければ空。
     */
    std::string EnteredAnimState(CCL::ECS::EntityID playerID);

    // --- イベントで立てる印（Update で取り出して消す） ---
    // イベントを出す System はどれもこの System より前のステージで走る。
    // ステージどうしは順に走るので、Update と同時に書かれることはなく、普通の変数でよい
    bool               m_justEvaded  = false;                     // ジャスト回避が成立した
    int                m_playerHits  = 0;                         // プレイヤーの攻撃が当たった回数
    int                m_dashed      = 0;                         // 回避ダッシュを出した回数
    int                m_jumpedStage = 0;                         // 跳んだジャンプの段（1 = 1段目、2 = 2段目。跳んでいなければ 0）
    CCL::ECS::EntityID m_playerID    = CCL::ECS::InvalidEntityID; // 命中・ダッシュの印を立てる相手

    // --- 項目の中の状態（Advance で消す） ---
    float m_stepTimer      = 0.0f;  // 今の項目に入ってからの実時間（秒）
    float m_clearTimer     = 0.0f;  // 達成した後の余韻の残り（秒・実時間）。0 より大きい間は判定せず、終わったら次の項目へ
    bool  m_sawAttackShape = false; // 回避の項目で、攻撃の陣形を1度でも見たか
    float m_moveDistance   = 0.0f;  // 移動の項目で、動いた距離（m）
    float m_lookAngle      = 0.0f;  // 視点の項目で、カメラを回した角度（度）
    int   m_dashCount      = 0;     // ダッシュの項目で、出した回数
    int   m_hitCount       = 0;     // 攻撃の項目で、当てた回数
    bool  m_comboStarted   = false; // コンボの項目で、この項目に入ってからコンボの1段目を出したか

    // --- 前のフレームの値（差や切り替わりを見るために持つ） ---
    DirectX::SimpleMath::Vector3 m_lastPlayerPos;          // プレイヤーの足元の位置
    float                        m_lastCameraYaw = 0.0f;   // カメラの水平角（度）
    uint32_t                     m_lastAnimState = 0;      // プレイヤーのアニメのステート
    bool                         m_hasLastSample = false;  // 上の値を記録済みか。false の間は差を求めない

    // --- 回避の補助スロー ---
    int   m_assistCount   = 0;     // 補助した回数
    bool  m_assistRunning = false; // 補助スローを掛けているか
    bool  m_assistNow     = false; // 「今だ」の段階に入ったか
    float m_assistTimer   = 0.0f;  // 補助スローの残り（秒・実時間）
    bool  m_assistDashed  = false; // 補助中にダッシュを始めたか

    // 突撃中のドローン1機ぶんの動き（SampleDroneMotion が書く）
    struct DroneMotion
    {
        DirectX::SimpleMath::Vector3 position;            // 前のフレームの位置（ワールド）
        DirectX::SimpleMath::Vector3 velocity;            // 実際の速度（m/秒・ゲーム時間）
        bool                         hasVelocity = false; // 2フレーム以上見て、速度が求まったか
    };
    std::unordered_map<CCL::ECS::EntityID, DroneMotion> m_droneMotion; // 突撃中のドローンごとの動き
};
