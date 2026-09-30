#pragma once
// ===========================================================================
// 概要: BT が出した技(BossCommandComponent)を、ボス本体の体の動き（速度・向き）に変える System。
//
// 責務:
//   - currentActionId を BossActionState に変換し、技に入った瞬間に進行方向などを決める
//   - 待機・移動・後方ステップ・跳躍・突進を1フレーム分進め、CharacterMovementComponent に書く
//   - 向きを Transform と Jolt の両方に書く
//   - 演出中や BT の一時停止中は待機にする
//
// 備考:
//   流れは BossCommand → BossActionSystem → CharacterMovementComponent
//   → JoltCharacterUpdateSystem（CharacterVirtual）。
//   実行ステージは L02_PostUpdate。BehaviorTreeSystem(L02_Update) の後、物理(L03_PrePhysics) の前。
//
//   BT を止めても最後に書かれた currentActionId は残るので、そのままだとこの System が
//   それを毎フレーム実行し続ける（演出中にボスが歩いてくる）。止まっている間は待機(None)にする。
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Engine/Time/TimeState.h"

struct BattleContext;

class BossActionSystem : public CCL::ECS::IfSystem<
                             BossActionSystem,
                             CCL::ECS::Write<CharacterMovementComponent>,
                             CCL::ECS::Write<BossActionComponent>,
                             CCL::ECS::Write<TransformComponent>,
                             CCL::ECS::Read<BossCommandComponent>,
                             CCL::ECS::Read<JoltCharacterConfigComponent>,
                             CCL::ECS::Read<JoltCharacterHandleComponent>,
                             CCL::ECS::Read<TimeStateComponent>>
{
    using Base = CCL::ECS::IfSystem<
        BossActionSystem,
        CCL::ECS::Write<CharacterMovementComponent>,
        CCL::ECS::Write<BossActionComponent>,
        CCL::ECS::Write<TransformComponent>,
        CCL::ECS::Read<BossCommandComponent>,
        CCL::ECS::Read<JoltCharacterConfigComponent>,
        CCL::ECS::Read<JoltCharacterHandleComponent>,
        CCL::ECS::Read<TimeStateComponent>>;

public:
    BossActionSystem() : IfSystem("BossActionSystem") {}
    ~BossActionSystem() override = default;

    // 全ボスについて、技の切り替わりを検出し、今の動きを1フレーム分進める。
    void Update(float dt) override;

    // デバッグ用。ボスの位置をインスペクタから直接動かせる。
    void OnGui() override;

    // Update が BehaviorTreeComponent::debugPaused を GetComponent で読むので、申告だけを足す。
    // テンプレート引数には足さない（ForEach の絞り込みにもなるので、BT を持たない
    // 検証用のボスでこの System が何も言わずに動かなくなる）。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    // 待機。足を止め、idleTurnSpeed でゆっくり相手へ向き直る。
    void ProcessIdle(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 相手へ歩いて近づく。acceleration で walkSpeed まで加速する。
    void ProcessMove(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterConfigComponent &config,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 横歩き（様子見）。相手への方向の直角へ進み続けるので、相手を中心に円を描く。
    // 向きは進行方向ではなく相手へ向け続ける。壁際では回る向きを内側へ反転する。
    void ProcessStrafe(BossActionComponent &bossDef, TransformComponent &trans,
                       CharacterMovementComponent         &movement,
                       const JoltCharacterConfigComponent &config,
                       const JoltCharacterHandleComponent &joltHandle,
                       const DirectX::SimpleMath::Vector3 &playerPos,
                       const BattleContext &battle, float dt);

    // 後方ステップ。減速しながら弧を描いて後ろへ下がり、その後は硬直する。
    // 退きながらも相手を向き続ける。背を向けると逃げたように見え、間合いを取り直す技だと読めなくなる。
    void ProcessBackStep(BossActionComponent &bossDef, TransformComponent &trans,
                         CharacterMovementComponent         &movement,
                         const JoltCharacterHandleComponent &joltHandle,
                         const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    /**
     * @brief 跳躍。溜め → 飛行 → 着地硬直。跳躍系の技はすべてここを通り、違いは着地点だけ。
     *
     * 飛行中は「今あるべき位置」を曲線から求め、そこへ届く速度を毎フレーム与える
     * （速度を足していく方式だと、壁や相手に触れたずれが着地点まで残る）。
     */
    void ProcessLeap(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext &battle, float dt);

    // 突進攻撃。後ろへ浮きながら下がる → 空中で止まって狙う → 下りながら直進 → 硬直。
    // 跳躍と同じく「今あるべき位置」へ届く速度を毎フレーム与える。
    void ProcessDash(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext &battle, float dt);

    // 構え（Windup）に入る瞬間に1回だけ呼ぶ。相手への方向を固定し、その方向へ dashDistance 進む終点と時間を決める。
    void LockDashAim(BossActionComponent                &bossDef,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext                &battle) const;

    // 踏み切りの瞬間に1回だけ呼ぶ。着地点（ステージ内に収めた値）と飛ぶ向きを決める。
    void BeginLeapFlight(BossActionComponent                &bossDef,
                         const DirectX::SimpleMath::Vector3 &takeoffPos,
                         const DirectX::SimpleMath::Vector3 &playerPos,
                         const BattleContext                &battle) const;

    // 中央を挟んで相手と反対側の着地点（中心から 半径×escapeLandingRatio）。
    // 高さは踏み切り地点にそろえる。
    DirectX::SimpleMath::Vector3 AwayToCenterLandingPoint(const BossActionComponent          &bossDef,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos,
                                                          const BattleContext                &battle) const;

    // 相手の位置から attackStandOff だけ手前の着地点。距離の上限は無い。
    DirectX::SimpleMath::Vector3 TowardTargetLandingPoint(const BossActionComponent          &bossDef,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos) const;

    // 点がステージの円（BattleContext::arenaRadius − 壁からの余白）の外なら、
    // 中心へ向かって円の縁まで引き戻す。跳躍の着地点と、突進の狙う位置・終点に使う。
    DirectX::SimpleMath::Vector3 ClampInsideArena(const DirectX::SimpleMath::Vector3 &point,
                                                  const BattleContext                &battle) const;

    // 飛行の進み具合 u（0=踏み切り, 1=着地）での位置。水平は等速、垂直は放物線。
    DirectX::SimpleMath::Vector3 LeapPositionAt(const BossActionComponent &bossDef, float u) const;

    // BT が出した技(ActionID) を、体の動き(BossActionState) へ変換する。知らない技は None。
    BossActionState TranslateAction(ActionID actionId) const;

    /**
     * @brief 技に入った瞬間だけ走る処理。タイマーを 0 に戻し、進行方向を決める。
     *
     * 跳躍系は、どの技かで着地点の決め方と調整値を選ぶので actionId も受け取る。
     * actionDuration は技の全体時間（BT ノードの param）。滞空で頂点に留まる長さを求めるのに使う。
     *
     * @note 方向をここで1回だけ決めるので、動く技は後から相手を追わず、避けられる。
     */
    void OnEnterState(BossActionState next, ActionID actionId, float actionDuration, BossActionComponent &bossDef,
                      const TransformComponent           &trans,
                      const DirectX::SimpleMath::Vector3 &playerPos) const;
};
