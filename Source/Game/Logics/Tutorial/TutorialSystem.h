#pragma once
// ===========================================================================
// 概要: チュートリアルシーンで、操作を1つずつ教える System
//
// 責務:
//   - プレイヤーの行動（移動・視点・ダッシュ・ジャンプ・ロックオン・命中・出た攻撃・ジャスト回避）を見て、
//     項目の達成の条件（TutorialGoal）を判定し、次の項目へ進める
//   - いまヒントを画面に出すべき場面かを決める（陣形が組まれ始めてから回避を促す、など）
//   - 回避を教えている間、突撃が当たる直前に世界を強くスローにして、押す瞬間を教える
//
// 備考: 文言と順番は TutorialSteps.h の表、達成の量はこの .cpp の先頭の定数と EvaluateGoal が持つ
//       ここは TutorialState を書くだけで、ヒントの見た目（TutorialHintHudSystem）とボス（BossPhaseSystem）は触らない
//       TutorialScene だけが登録する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Tutorial/TutorialState.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

#include <SimpleMath.h>
#include <string>
#include <unordered_map>
#include <vector>

class TutorialSystem : public CCL::ECS::IfSystem<
    TutorialSystem>
{
public:
    TutorialSystem() : IfSystem("TutorialSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    void OnGui() override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    // このフレームまでに届いたイベント。Update の頭で取り出して空に戻す
    struct FrameEvents
    {
        bool justEvaded  = false; // ジャスト回避が成立した
        int  playerHits  = 0;     // プレイヤーの攻撃が当たった回数
        int  dashed      = 0;     // 回避ダッシュを出した回数
        int  jumpedStage = 0;     // 跳んだジャンプの段（1 か 2。跳んでいなければ 0）
    };

    // 突撃中のドローン1機ぶんの動き（SampleDroneMotion が書く）
    struct DroneMotion
    {
        DirectX::SimpleMath::Vector3 position;            // 前のフレームの位置（ワールド）
        DirectX::SimpleMath::Vector3 velocity;            // 実際の速度（m/秒・ゲーム時間）
        bool                         hasVelocity = false; // 2フレーム以上見て、速度が求まったか
    };

    // --- 達成の判定 ---

    // 今の項目を達成したかを返し、帯の進み具合を書く。ジャスト回避の項目はヒントを出すかも決め直す
    bool EvaluateGoal(TutorialState &state, const TutorialStepDef &def, const BattleContext &battle,
                      const FrameEvents &events, float dt);

    // ヒントの帯に出す進み具合（done / goal、0〜1 に収める）を書く
    void WriteProgress(TutorialState &state, float done, float goal) const;
    void WriteProgress(TutorialState &state, int done, int goal) const;

    // --- 項目の進行 ---

    // 次の項目へ進める。最後の項目の次は教え終えた状態になる
    void Advance(TutorialState &state);

    // 指定の項目（kTutorialSteps の添字）へ移り、項目内のタイマーと記録を消す。kTutorialStepCount なら教え終える
    void JumpToStep(TutorialState &state, uint8_t step);

    // チュートリアルを最初から始める（デバッグ用）。補助スローの回数も戻す
    // 最初の項目はボスを待たせる項目なので、BossPhaseSystem がボスを TutorialWait にする
    void Restart(TutorialState &state);

    // --- 動いた量・回した量・出た攻撃 ---

    // 移動の項目で、この項目に入ってから水平に動いた距離（m）を足していき、その合計を返す
    float AccumulateMove(const BattleContext &battle);

    // 視点の項目で、カメラをこの項目に入ってから左右に回した角度（度）を足していき、合計を返す。往復も足す
    float AccumulateLook(CCL::ECS::EntityID playerID);

    // プレイヤーのアニメが、このフレームに入ったステートの名前を返す。切り替わっていなければ空
    // どの攻撃が出るかはアニメグラフが決めるので、コンボの段や派生、ダッシュ攻撃はステートで見る
    std::string EnteredAnimState(CCL::ECS::EntityID playerID);

    // 本線のコンボの段（0〜8）を、このフレームに入ったステートから進めて返す
    // 今の段の次に入ったら1つ上げ、本線でないステートへ移ったら 0 に戻す。Atk_Combo1 に入ったら 1 から数え直す
    int AdvanceMainCombo(const std::string &entered, int stage) const;

    // 2手で出す技（コンボの派生・空中攻撃・ダッシュ攻撃からのコンボ）を見て、出し切ったかを返す
    // first に入ったら帯を半分にし、そのまま second に入ったら達成。間に別のステートへ移ったら最初から
    bool TrackTwoStep(TutorialState &state, const std::string &entered, const char *first, const char *second);

    // --- ジャスト回避の項目 ---

    // ボスがいま攻撃の陣形を組んでいるか（組み始め〜撃ち終わり）。回避のヒントを出す場面
    bool IsBossAttackFormationActive() const;

    // 回避を教えている間の補助スローを進める。押すべき瞬間を体で覚えてもらうためのもの
    //   予告: 当たる kAssistWarnTime 秒前から、世界を kAssistWarnScale まで遅くする
    //   今だ: 当たる kAssistNowTime 秒前から、ほぼ止めて「今だ！」を出す
    // 成功するまで何度でも掛ける。realDt はスローの掛かっていない経過時間
    void TickEvadeAssist(TutorialState &state, float realDt);

    // 補助スローを終え、すぐ等速に戻す
    void EndAssist();

    // 突撃中のドローンの位置を記録し、前のフレームとの差から実際の速度を求める。突撃をやめた機の記録は消す
    // currentVelocity は使えない。RigidFollow では位置を持ち場へ直接合わせていて、実際の速さが入っていない
    void SampleDroneMotion();

    // 突撃中のドローンのうち、最も早くプレイヤーに当たるものまでの残り時間（秒・ゲーム時間）。なければ負の値
    // 今の速度のまま進んだときに最も近づく距離が kAssistMissRadius 以内の機だけを、当たるとみなす
    float TimeToStrike() const;

    // --- イベントで立てる印 ---
    // イベントを出す System はどれもこの System より前のステージで走る
    // ステージどうしは順に走るので、Update と同時に書かれることはなく、普通の変数でよい
    FrameEvents        m_pendingEvents;                         // まだ Update で読んでいないイベント
    CCL::ECS::EntityID m_playerID = CCL::ECS::InvalidEntityID; // 命中・ダッシュの印を立てる相手

    // --- 項目の中の状態（JumpToStep で消す） ---
    float m_stepTimer      = 0.0f;  // 今の項目に入ってからの実時間（秒）
    float m_clearTimer     = 0.0f;  // 達成後の余韻の残り（秒・実時間）。この間は判定しない
    bool  m_sawAttackShape = false; // 回避の項目で、攻撃の陣形を1度でも見たか
    float m_moveDistance   = 0.0f;  // 移動の項目で、動いた距離（m）
    float m_lookAngle      = 0.0f;  // 視点の項目で、カメラを回した角度（度）
    int   m_dashCount      = 0;     // ダッシュの項目で、出した回数
    int   m_hitCount       = 0;     // 攻撃の項目で、当てた回数
    int   m_chainStage     = 0;     // 攻撃の項目で、この項目に入ってから続けて出せた手の数

    // --- 前のフレームの値（差や切り替わりを見るために持つ） ---
    // 1つの項目で使うのはどれか1つだけなので、記録済みの印は共有する
    DirectX::SimpleMath::Vector3 m_lastPlayerPos;         // プレイヤーの足元の位置
    float                        m_lastCameraYaw = 0.0f;  // カメラの水平角（度）
    uint32_t                     m_lastAnimState = 0;     // プレイヤーのアニメのステート
    bool                         m_hasLastSample = false; // 上の値を記録済みか。false の間は差を求めない

    // --- 回避の補助スロー ---
    int   m_assistCount   = 0;     // 補助した回数（デバッグ表示用）
    bool  m_assistRunning = false; // 補助スローを掛けているか
    bool  m_assistNow     = false; // 「今だ」の段階に入ったか
    float m_assistTimer   = 0.0f;  // 補助スローの残り（秒・実時間）
    bool  m_assistDashed  = false; // 補助中にダッシュを始めたか

    std::unordered_map<CCL::ECS::EntityID, DroneMotion> m_droneMotion; // 突撃中のドローンごとの動き
};
