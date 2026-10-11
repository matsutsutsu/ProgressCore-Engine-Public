#pragma once
// ===========================================================================
// 概要: ジャスト回避成立後の「ラッシュ」を駆動する System。
//
// 責務:
//   - ジャスト回避の成功(JustEvadeSuccessEvent)を受けて、ダッシュを中断しラッシュを開始する
//   - ラッシュ中は世界を一律スローにし、プレイヤーだけ実時間へ戻す
//   - ラッシュ中は常に無敵を維持する
//   - フェーズ（回避離脱→待機→攻撃→自由）を進める
//   - ラッシュ中かどうかを画面演出(JustEvadeEffectComponent)へ伝える
//
// 備考: 回避ダッシュ本体と歩走は PlayerDashSystem（別System）。ビネットは JustEvadeEffectSystem(R02)。
//       この System は PlayerDashSystem の後に走り、ラッシュ中は移動を上書きする。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Player/Components/RushComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

#include <vector>

struct DashComponent;

class PlayerRushSystem : public CCL::ECS::IfSystem<PlayerRushSystem,
                                                   CCL::ECS::Write<RushComponent>,
                                                   CCL::ECS::Write<TimeStateComponent>,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Write<AnimParametersComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerRushSystem() : IfSystem("PlayerRushSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    // ラッシュの状態と「移動の指令と結果」をインスペクタに出す（離脱/間合い詰めが効かない時の切り分け用）。
    void OnGui() override;

    // Filter 外へ書く型を申告する（画面演出のシングルトン・開始時に中断する DashComponent）。
    // 宣言しないと、同じ型を触る他 System との並列実行で競合を検出できない。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // 移動(CharacterMovementComponent)を PlayerDashSystem と同じ L02_Update で書くので、順番を宣言する。
    std::vector<const char *> GetRunAfter() const override { return {"PlayerDashSystem"}; }

private:
    // 基底の Write 申告を足し込むためのエイリアス。
    using Base = CCL::ECS::IfSystem<PlayerRushSystem,
                                    CCL::ECS::Write<RushComponent>,
                                    CCL::ECS::Write<TimeStateComponent>,
                                    CCL::ECS::Write<CharacterMovementComponent>,
                                    CCL::ECS::Write<PlayerComponent>,
                                    CCL::ECS::Write<AnimParametersComponent>,
                                    CCL::ECS::Read<PlayerInputComponent>,
                                    CCL::ECS::Read<TransformComponent>>;

    // ラッシュを開始する（ダッシュを中断し、ダッシュ方向を離脱方向にして回避離脱から始める）。
    void StartRush(RushComponent &rush, DashComponent &dash);

    /**
     * @brief 現フェーズの残り時間が尽きたときに、次のフェーズへ時間遷移させる。
     *
     * 回避離脱 → 待機 → 自由、攻撃 → 自由、自由 → 終了。待機→攻撃は時間でなく攻撃入力で起きる。
     *
     * @param rush ラッシュの状態（phase / timer を書く）。
     * @param time プレイヤーの時間の状態（終了時に cinematicScale を戻す）。
     */
    void AdvancePhase(RushComponent &rush, TimeStateComponent &time);

    /**
     * @brief ラッシュ中の時間制御を1フレーム進める。
     *
     * 一律の世界スローを維持しつつ、cinematicScale でプレイヤーだけ実時間へ戻す。
     *
     * @param rush    ラッシュの状態（worldScale / playerScale を読む）。
     * @param time    プレイヤーの時間の状態（cinematicScale を書く。group を読む）。
     * @param timeCtx 全体の時間倍率。nullptr なら等速とみなす。
     */
    void DriveTimeScale(RushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

    // 回避離脱（Locked）。残り時間から進行度を出し、EaseOut で初速から 0 へ落としながら前進させる。
    void DriveLeap(RushComponent &rush, CharacterMovementComponent &movement);

    // 攻撃状態（Attack）に入る。詰め先（ロックオン敵 or 向いている方向）を決める。
    void EnterAttack(RushComponent &rush, const PlayerComponent &player);

    /**
     * @brief 攻撃状態（Attack）を1フレーム進める。詰め → 斬り の順で、System 内で完結させる。
     *
     * 斬り前は closeInSpeed で詰める（ロックオン中は毎フレーム敵へ向け直す）。間合い（closeInStopDistance）
     * に入る／詰め先が無ければ、その場で斬りを1回出し、以降は攻撃アニメ（ルートモーションの踏み込み）に移動を委ねる。
     *
     * @param rush      ラッシュの状態（closeInDir / closeInTarget / attackFired を読み書き）。
     * @param movement  動かす側への指令（desiredVelocity / overrideSpeed を書く）。
     * @param player    向き直りの yaw（attackFaceYaw / attackFaceTimer）を書く先。
     * @param animParams 斬りのトリガーを出す先。
     * @param playerPos プレイヤーのワールド位置。敵との距離・方向を測るのに使う。
     */
    void DriveCloseIn(RushComponent &rush, CharacterMovementComponent &movement,
                      PlayerComponent &player, AnimParametersComponent &animParams,
                      const DirectX::SimpleMath::Vector3 &playerPos);

    // 斬りを1回出す（Attack トリガー＋対象への向き直り）。以降は攻撃アニメに移動を委ねる。
    void FireStrike(RushComponent &rush, PlayerComponent &player, AnimParametersComponent &animParams);

    // プレイヤーをその場で止める（移動の意思を打ち消す）。待機フェーズの操作不能に使う。
    void StopPlayer(CharacterMovementComponent &movement);

    // ラッシュ中、被弾処理が見る無敵タイマーを invincibleRefresh 以上に保つ。
    void MaintainInvincibility(CCL::ECS::EntityID id, const RushComponent &rush);

    // ラッシュを終える（スローの要求をやめ、時間倍率を戻す）。
    void EndRush(RushComponent &rush, TimeStateComponent &time);

    // 間合い詰めの詰め先を選ぶ。ロックオン中で生存している敵。無ければ InvalidEntityID。
    CCL::ECS::EntityID PickCloseInTarget(const PlayerComponent &player) const;

    // 詰め先がまだ有効（存在し生存中）かを返す。
    bool IsCloseInTargetAlive(CCL::ECS::EntityID id) const;

    // フェーズ名をインスペクタ表示用の文字列にする。
    const char *PhaseLabel(RushPhase phase) const;

    // ジャスト回避の成功で届いた「開始したいプレイヤー」。別タスクで届くので溜めて Update で開始する。
    std::vector<CCL::ECS::EntityID> m_pendingStarts;
};
