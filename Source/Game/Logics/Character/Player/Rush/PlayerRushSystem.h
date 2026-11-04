#pragma once
// ===========================================================================
// 概要: ジャスト回避が成立した後の「ラッシュ」を進める System。
//
// 責務:
//   - ジャスト回避の成立を受けて、ダッシュを打ち切りラッシュを始める
//   - ラッシュ中は世界をスローにし、プレイヤーだけ実時間に戻す
//   - ラッシュ中は無敵を保つ
//   - フェーズ（回避離脱 → 待機 → 攻撃 → 連撃）を進める。待機で攻撃しなければ、その場で終える
//   - ラッシュ中かを画面演出（PlayerRushEffectComponent）へ伝える
//
// 備考:
//   PlayerDashSystem と PlayerMoveSystem の後に走り、ラッシュ中は移動を上書きする。
//   回避離脱は始めるときに MoveInDirection で予約し、物理が運び終えたら待機へ進める。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

#include <vector>

struct PlayerDashComponent;
struct AnimStateMachineComponent;

class PlayerRushSystem : public CCL::ECS::IfSystem<PlayerRushSystem,
                                                   CCL::ECS::Write<PlayerRushComponent>,
                                                   CCL::ECS::Write<TimeStateComponent>,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Write<AnimParametersComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<TransformComponent>,           // 詰め先の敵の位置
                                                   CCL::ECS::Read<JoltCharacterHandleComponent>> // 自分の物理の位置
{
public:
    PlayerRushSystem() : IfSystem("PlayerRushSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    // ラッシュの状態と、移動の依頼と結果をインスペクタに出す
    // 離脱や詰めが効かないときの切り分けに使う
    void OnGui() override;

    // 画面演出のシングルトンと、開始時に打ち切る PlayerDashComponent も書く
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // 連撃の終わりを決めるのに、アニメのステート（AnimStateMachineComponent）も読む
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // 同じ L02_Update で移動を書く2つより後に走り、ラッシュ中は上書きする
    std::vector<const char *> GetRunAfter() const override { return {"PlayerDashSystem", "PlayerMoveSystem"}; }

    // 出したトリガーを、同じステップのうちにアニメの遷移へ使わせる
    std::vector<const char *> GetRunBefore() const override { return {"AnimControllerSystem"}; }

private:
    using Base = CCL::ECS::IfSystem<PlayerRushSystem,
                                    CCL::ECS::Write<PlayerRushComponent>,
                                    CCL::ECS::Write<TimeStateComponent>,
                                    CCL::ECS::Write<CharacterMovementComponent>,
                                    CCL::ECS::Write<PlayerComponent>,
                                    CCL::ECS::Write<AnimParametersComponent>,
                                    CCL::ECS::Read<PlayerInputComponent>,
                                    CCL::ECS::Read<TransformComponent>,
                                    CCL::ECS::Read<JoltCharacterHandleComponent>>;

    // ラッシュを始める
    // ダッシュを打ち切り、ダッシュの向きへ離れる移動を予約して、回避離脱から始める
    void StartRush(PlayerRushComponent &rush, PlayerDashComponent &dash, CharacterMovementComponent &movement);

    /**
     * @brief 次のフェーズへ進める。
     *
     * 回避離脱 → 待機、待機 → 終了、攻撃 → 連撃（詰めきれなければ終了）、連撃 → 終了。
     * 待機 → 攻撃は EnterAttack が行う。
     * 回避離脱は予約した移動が終わったとき、それ以外は残り時間が尽きたときに呼ぶ。
     * 連撃の残り時間は、ラッシュのアニメから抜けたときに TrackRushAnim が 0 にする。
     *
     * @param rush ラッシュの状態。phase と timer を書く。
     */
    void AdvancePhase(PlayerRushComponent &rush);

    /**
     * @brief 世界のスローを保ち、プレイヤーの時計だけを実時間へ戻す。
     * @param rush    worldScale・playerScale を持つラッシュの状態。
     * @param time    プレイヤーの時間の状態。cinematicScale を書き、group を読む。
     * @param timeCtx 全体の時間倍率。nullptr なら等速とみなす。
     */
    void DriveTimeScale(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

    /**
     * @brief 終えた後、世界のスローが戻る間もプレイヤーだけを実時間で動かす。戻りきったら補正を外す。
     *
     * 終えた瞬間に補正を外すと、プレイヤーまで世界と同じスローに落ち、そこから世界と一緒に戻る。
     * とどめの後に、体だけが一瞬スローモーションになってしまう。
     *
     * @param rush    ラッシュの状態。戻りきったら isRecovering を倒す。
     * @param time    プレイヤーの時間の状態。cinematicScale を書く。
     * @param timeCtx 全体の時間倍率。nullptr なら等速とみなす。
     */
    void DriveRecovery(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

    // プレイヤーに掛かっている世界の時間倍率（全体 × 勢力）。timeCtx が無ければ 1
    float CurrentWorldScale(const TimeStateComponent &time, const TimeContext *timeCtx) const;

    // 攻撃（Attack）に入る。詰め先（ロックオン中の敵、無ければ今の正面）を決める
    void EnterAttack(PlayerRushComponent &rush, const PlayerComponent &player);

    /**
     * @brief 攻撃（Attack）を1ステップ分進める。詰めてから1回だけ斬る。
     *
     * 斬るまでは closeInSpeed で詰め、ロックオン中は毎ステップ敵へ向け直す。
     * 間合いに入るか詰め先が無ければ斬り、以後の移動は攻撃アニメのルートモーションに任せる。
     *
     * @param rush       ラッシュの状態。closeInDir・closeInTarget・attackFired を読み書きする。
     * @param movement   詰める向きと速さを書く先。
     * @param player     向き直りの yaw を書く先。
     * @param animParams 斬りのトリガーを引く先。
     * @param playerPos  プレイヤーの物理の位置（ワールド）。
     */
    void DriveCloseIn(PlayerRushComponent &rush, CharacterMovementComponent &movement,
                      PlayerComponent &player, AnimParametersComponent &animParams,
                      const DirectX::SimpleMath::Vector3 &playerPos);

    // 詰めの斬り（通常のダッシュ攻撃）を出す（RushStrike のトリガーと、詰める向きへの向き直り）
    void FireStrike(PlayerRushComponent &rush, PlayerComponent &player, AnimParametersComponent &animParams);

    // 連撃に入る。終わりはアニメで決めるので、残り時間には上限（flurryMaxDuration）だけを入れる
    void EnterFlurry(PlayerRushComponent &rush);

    /**
     * @brief 次の斬りを頼む。斬りを rushStepCount 回出した後は、とどめを頼む。
     * @param rush       ラッシュの状態。rushStep で、斬りかとどめかを決める（数えるのは TrackRushAnim）。
     * @param player     詰め先への向き直りを書く先。
     * @param animParams RushAttack か RushFinish のトリガーを引く先。
     * @param playerPos  プレイヤーの物理の位置（ワールド）。向き直る向きを求める。
     */
    void RequestNextStep(PlayerRushComponent &rush, PlayerComponent &player,
                         AnimParametersComponent &animParams, const DirectX::SimpleMath::Vector3 &playerPos);

    /**
     * @brief 連撃中のアニメを見て、斬りに入った数を数え、ラッシュのアニメから抜けたら連撃を終える。
     * @param rush ラッシュの状態。rushStep と lastAnimState を書き、抜けたら timer を 0 にする。
     * @param fsm  プレイヤーのアニメのステートマシン。無ければ抜けたものとして扱う。
     */
    void TrackRushAnim(PlayerRushComponent &rush, const AnimStateMachineComponent *fsm);

    // ラッシュ中は、被弾処理が見る無敵タイマーを invincibleRefresh 以上に保つ
    void MaintainInvincibility(CCL::ECS::EntityID id, const PlayerRushComponent &rush);

    // ラッシュを終え、世界のスローを戻し始める。とどめで終えたときは finishRecover かけてゆっくり戻す
    void EndRush(PlayerRushComponent &rush);

    // ロックオン中で生きている敵を返す。無ければ InvalidEntityID
    CCL::ECS::EntityID PickCloseInTarget(const PlayerComponent &player) const;

    // 詰め先が今も居て、生きているか
    bool IsCloseInTargetAlive(CCL::ECS::EntityID id) const;

    // インスペクタに出すフェーズ名
    const char *PhaseLabel(RushPhase phase) const;

    std::vector<CCL::ECS::EntityID> m_pendingStarts; // ジャスト回避が成立したプレイヤー。Update で始めるまで溜める
};
