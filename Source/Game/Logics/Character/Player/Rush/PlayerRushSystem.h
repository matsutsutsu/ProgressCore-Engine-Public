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

    // 同じ L02_Update で移動を書く2つより後に走り、ラッシュ中は上書きする
    std::vector<const char *> GetRunAfter() const override { return {"PlayerDashSystem", "PlayerMoveSystem"}; }

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
     * 回避離脱 → 待機、待機 → 終了、攻撃 → 連撃、連撃 → 終了。待機 → 攻撃は EnterAttack が行う。
     * 回避離脱は予約した移動が終わったとき、それ以外は残り時間が尽きたときに呼ぶ。
     *
     * @param rush ラッシュの状態。phase と timer を書く。
     * @param time プレイヤーの時間の状態。終わるときに cinematicScale を戻す。
     */
    void AdvancePhase(PlayerRushComponent &rush, TimeStateComponent &time);

    /**
     * @brief 世界のスローを保ち、プレイヤーの時計だけを実時間へ戻す。
     * @param rush    worldScale・playerScale を持つラッシュの状態。
     * @param time    プレイヤーの時間の状態。cinematicScale を書き、group を読む。
     * @param timeCtx 全体の時間倍率。nullptr なら等速とみなす。
     */
    void DriveTimeScale(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

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

    // 連撃に入る。次の攻撃までの猶予を満たし、連撃の経過を 0 から数える
    void EnterFlurry(PlayerRushComponent &rush);

    /**
     * @brief ラッシュの次の段を出す。段の数の上限まで出した後は何もしない。
     * @param rush       ラッシュの状態。rushStep を進め、timer に次の猶予か、最後の段の後の時間を書く。
     * @param player     詰め先への向き直りを書く先。
     * @param animParams RushAttack のトリガーを引く先。
     * @param playerPos  プレイヤーの物理の位置（ワールド）。向き直る向きを求める。
     */
    void FireNextStep(PlayerRushComponent &rush, PlayerComponent &player,
                      AnimParametersComponent &animParams, const DirectX::SimpleMath::Vector3 &playerPos);

    // ラッシュ中は、被弾処理が見る無敵タイマーを invincibleRefresh 以上に保つ
    void MaintainInvincibility(CCL::ECS::EntityID id, const PlayerRushComponent &rush);

    // ラッシュを終え、プレイヤーの時間倍率を戻す
    void EndRush(PlayerRushComponent &rush, TimeStateComponent &time);

    // ロックオン中で生きている敵を返す。無ければ InvalidEntityID
    CCL::ECS::EntityID PickCloseInTarget(const PlayerComponent &player) const;

    // 詰め先が今も居て、生きているか
    bool IsCloseInTargetAlive(CCL::ECS::EntityID id) const;

    // インスペクタに出すフェーズ名
    const char *PhaseLabel(RushPhase phase) const;

    std::vector<CCL::ECS::EntityID> m_pendingStarts; // ジャスト回避が成立したプレイヤー。Update で始めるまで溜める
};
