// AIの出力(BossCommandComponent)を読み取り、ボス本体の物理移動に変換する「筋肉」。
// 脳（BehaviorTreeSystem）とは分離し、「意図を物理に変換するだけ」に責務を限る。
//
//   BossCommand → BossActionSystem → CharacterMovementComponent
//   → JoltCharacterUpdateSystem（CharacterVirtual）
//
// 実行順序は L02_PostUpdate。脳(L02_Update)の後、物理(L03_PrePhysics)の前。
//
// 脳が止まったら筋肉も止める。
//   BTを凍らせても最後に書かれた currentActionId は残るので、この層は
//   それを毎フレーム実行し続けてしまう（演出中にボスが歩いてくる事故が実際に出た）。
//   凍っている間は待機(None)へ倒す。
#pragma once
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

/// ボスの「今どの技を出しているか」(currentActionId) を、体の動き（速度・向き）へ変換する。
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

    /// 全ボスについて、技の切り替わりを検知し、今の動きを1フレーム分進める。
    void Update(float dt) override;

    /// デバッグ用。ボスの位置をインスペクタから直接動かせる。
    void OnGui() override;

    /// Update が BehaviorTreeComponent::debugPaused を GetComponent で読む。
    /// テンプレート引数には足さない（ForEach の絞り込みも兼ねるので、BTを持たない
    /// 検証用のボスでこの層が黙って動かなくなる）。申告だけをここで足す。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    /// 待機。足を止め、idleTurnSpeed でゆっくり相手へ向き直る。
    void ProcessIdle(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    /// 相手へ歩いて近づく。acceleration で walkSpeed まで加速する。
    void ProcessMove(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterConfigComponent &config,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    /// 後方ステップ。減速しながら弧を描いて後ろへ抜け、そのあと硬直する。
    /// 退きながらも相手を向き続ける。背を向けると「逃げた」に見え、間合いを切って
    /// 仕切り直す技だと読めなくなる。
    void ProcessBackStep(BossActionComponent &bossDef, TransformComponent &trans,
                         CharacterMovementComponent         &movement,
                         const JoltCharacterHandleComponent &joltHandle,
                         const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    /// 跳躍。溜め → 飛行 → 着地硬直。跳躍系の技はすべてここを通り、違いは着地点だけ。
    /// 飛行中は「今あるべき位置」を曲線から出し、そこへ届く速度を毎フレーム与える
    /// （速度を積み上げる方式だと、壁や相手に触れたずれが着地点まで残る）。
    void ProcessLeap(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext &battle, float dt);

    /// 突進攻撃。後ろへ浮きながら下がる → 空中で止まって狙う → 下りながら直進 → 硬直。
    /// 跳躍と同じく「今あるべき位置」へ届く速度を毎フレーム与える。
    void ProcessDash(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext &battle, float dt);

    /// 予告の終わりに1回だけ呼ぶ。相手への方向を固定し、その方向へ dashDistance 進む終点と時間を決める。
    void BeginDashCharge(BossActionComponent                &bossDef,
                         const DirectX::SimpleMath::Vector3 &playerPos,
                         const BattleContext                &battle) const;

    /// 踏み切りの瞬間に1回だけ呼ぶ。着地点（ステージ内に収めた値）と飛ぶ向きを確定させる。
    void BeginLeapFlight(BossActionComponent                &bossDef,
                         const DirectX::SimpleMath::Vector3 &takeoffPos,
                         const DirectX::SimpleMath::Vector3 &playerPos,
                         const BattleContext                &battle) const;

    /// 中央を挟んで相手と反対側の着地点（中心から 半径×escapeLandingRatio）。
    /// 高さは踏み切り地点に揃える。
    DirectX::SimpleMath::Vector3 AwayToCenterLandingPoint(const BossActionComponent          &bossDef,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos,
                                                          const BattleContext                &battle) const;

    /// 相手の位置から attackStandOff だけ手前の着地点。距離の上限は無い。
    DirectX::SimpleMath::Vector3 TowardTargetLandingPoint(const BossActionComponent          &bossDef,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos) const;

    /// 着地点がステージの円（BattleContext::arenaRadius − 壁からの余白）の外なら、
    /// 中心へ向かって円の縁まで引き戻す。どの着地点の決め方でも最後に通す。
    DirectX::SimpleMath::Vector3 ClampInsideArena(const DirectX::SimpleMath::Vector3 &point,
                                                  const BattleContext                &battle) const;

    /// 飛行の進み具合 u（0=踏み切り, 1=着地）での位置。水平は等速、垂直は放物線。
    DirectX::SimpleMath::Vector3 LeapPositionAt(const BossActionComponent &bossDef, float u) const;

    /// BTが出した技(ActionID) を、体の動き(BossActionState) へ翻訳する。知らない技は None。
    BossActionState TranslateAction(ActionID actionId) const;

    /// 技に入った瞬間だけ走る処理。タイマーを 0 に戻し、進行方向を確定させる。
    /// 跳躍系は、どの技かで着地点の決め方と調整値を選ぶので actionId も受け取る。
    /// actionDuration は技の全体時間（BTノードの param）。滞空が頂点の長さを逆算するのに使う。
    /// 方向をここで1回だけ決めるのが、動く技が「避けられる技」である根拠。
    void OnEnterState(BossActionState next, ActionID actionId, float actionDuration, BossActionComponent &bossDef,
                      const TransformComponent           &trans,
                      const DirectX::SimpleMath::Vector3 &playerPos) const;
};
