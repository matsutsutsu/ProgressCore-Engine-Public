#pragma once
// ===========================================================================
// 概要: ジャスト回避の成立後の「ラッシュ」を進める System
//
// 責務:
//   - ダッシュを打ち切り、ラッシュを始める
//   - ラッシュ中は世界をスローにし、プレイヤーだけ実時間・無敵で動かす
//   - フェーズを進める（回避離脱 → 待機 → 攻撃 → 連撃）
//   - ラッシュ中かを画面演出へ伝える
//
// 備考: 回避離脱の移動は開始時に MoveInDirection で予約し、物理が運び終えたら待機へ進める
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

class PlayerRushSystem : public CCL::ECS::IfSystem<
    PlayerRushSystem,
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
    void OnGui() override;

    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // 移動を書く2つより後に走り、ラッシュ中は上書きする
    std::vector<const char *> GetRunAfter() const override { return {"PlayerDashSystem", "PlayerMoveSystem"}; }

    // 出したトリガーを、同じステップのアニメの遷移に使わせる
    std::vector<const char *> GetRunBefore() const override { return {"AnimControllerSystem"}; }

private:
    using Base = CCL::ECS::IfSystem<
        PlayerRushSystem,
        CCL::ECS::Write<PlayerRushComponent>,
        CCL::ECS::Write<TimeStateComponent>,
        CCL::ECS::Write<CharacterMovementComponent>,
        CCL::ECS::Write<PlayerComponent>,
        CCL::ECS::Write<AnimParametersComponent>,
        CCL::ECS::Read<PlayerInputComponent>,
        CCL::ECS::Read<TransformComponent>,
        CCL::ECS::Read<JoltCharacterHandleComponent>>;

    // ============================================================
    // フェーズ
    // ============================================================

    // ラッシュを始める。ダッシュを打ち切り、回避離脱の移動を予約する
    void StartRush(PlayerRushComponent &rush, PlayerDashComponent &dash, CharacterMovementComponent &movement);

    // 次のフェーズへ進める
    // 回避離脱は移動が終わったとき、ほかは残り時間が尽きたときに呼ぶ
    void AdvancePhase(PlayerRushComponent &rush);

    // 連撃に入る
    // 終わりはアニメで決めるので、残り時間には上限だけを入れる
    void EnterFlurry(PlayerRushComponent &rush);

    // 連撃中のアニメから、入った斬りの数を数える
    // ラッシュのアニメから抜けたら連撃を終える。fsm が nullptr なら抜けたとみなす
    void TrackRushAnim(PlayerRushComponent &rush, const AnimStateMachineComponent *fsm);

    // 攻撃に入り、詰め先を決める。ロックオンがなければ今の正面へ詰める
    void EnterAttack(PlayerRushComponent &rush, const PlayerComponent &player);

    // ============================================================
    // 時間
    // ============================================================

    // 世界のスローを保ち、プレイヤーの時計だけを実時間へ戻す
    // timeCtx が nullptr なら等速とみなす
    void DriveTimeScale(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

    // 終了後、世界のスローが戻る間もプレイヤーを実時間で動かす
    // 戻りきったら補正を外し、isRecovering を倒す
    void DriveRecovery(PlayerRushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

    // プレイヤーに掛かる世界の時間倍率（全体 × 勢力）。timeCtx がなければ 1
    float CurrentWorldScale(const TimeStateComponent &time, const TimeContext *timeCtx) const;

    // ラッシュを終え、世界のスローを戻し始める
    void EndRush(PlayerRushComponent &rush);

    // ============================================================
    // 間合い詰め・連撃・無敵
    // ============================================================

    // 攻撃を1ステップ進める。詰め先へ詰めて、間合いに入ったら1回だけ斬る
    // playerPos はプレイヤーの物理の位置（ワールド）
    void DriveCloseIn(PlayerRushComponent &rush, CharacterMovementComponent &movement,
                      PlayerComponent &player, AnimParametersComponent &animParams,
                      const DirectX::SimpleMath::Vector3 &playerPos);

    // 詰めの斬り（通常のダッシュ攻撃）を出し、詰める向きへ向き直る
    void FireStrike(PlayerRushComponent &rush, PlayerComponent &player, AnimParametersComponent &animParams);

    // 次の斬りを頼む。rushStepCount 回斬った後は、とどめを頼む
    void RequestNextStep(PlayerRushComponent &rush, PlayerComponent &player,
                         AnimParametersComponent &animParams, const DirectX::SimpleMath::Vector3 &playerPos);

    // ラッシュ中は、被弾処理が見る無敵タイマーを invincibleRefresh 以上に保つ
    void MaintainInvincibility(CCL::ECS::EntityID id, const PlayerRushComponent &rush);

    // ============================================================
    // 詰め先・デバッグ UI
    // ============================================================

    // ロックオン中で生きている敵を返す。いなければ InvalidEntityID
    CCL::ECS::EntityID PickCloseInTarget(const PlayerComponent &player) const;

    // 詰め先がまだ存在して、生きているか
    bool IsCloseInTargetAlive(CCL::ECS::EntityID id) const;

    // インスペクタに出すフェーズ名
    const char *PhaseLabel(RushPhase phase) const;

    std::vector<CCL::ECS::EntityID> m_pendingStarts; // 成立を受けて、まだ始めていないプレイヤー
};
