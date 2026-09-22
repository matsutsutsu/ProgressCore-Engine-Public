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

    void Update(float dt) override;
    void OnGui() override;

    // Update が BehaviorTreeComponent::debugPaused を GetComponent で読む。
    //   テンプレート引数には足さない（ForEach の絞り込みも兼ねるので、BTを持たない
    //   検証用のボスでこの層が黙って動かなくなる）。申告だけをここで足す。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    void ProcessIdle(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    void ProcessMove(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterConfigComponent &config,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 薙ぎ払い中の物理挙動。足を止めてプレイヤーを向くだけ。
    void ProcessMeleeHold(BossActionComponent &bossDef, TransformComponent &trans,
                          CharacterMovementComponent         &movement,
                          const JoltCharacterHandleComponent &joltHandle,
                          const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 後方ステップ。減速しながら後ろへ抜け、そのあと硬直する。
    //
    // 退きながらプレイヤーを向き続ける。背を向けると「逃げた」に見えてしまい、
    // 間合いを切って仕切り直す技だと読めなくなる。
    //   空中は use3DVelocity で速度を直接与える。重力に任せると着地の時刻が
    //   キャラ設定に左右され、backStepTime と合わなくなる。
    void ProcessBackStep(BossActionComponent &bossDef, TransformComponent &trans,
                         CharacterMovementComponent         &movement,
                         const JoltCharacterHandleComponent &joltHandle,
                         const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 跳躍。溜め → 飛行 → 着地硬直。どの跳躍系の技もここを通り、違いは着地点だけ。
    //
    // 飛行中は「今あるべき位置」を曲線から出し、そこへ届く速度を毎フレーム与える。
    // 速度の式を積み上げる方式だと、壁や相手に触れたずれが着地点まで残る。
    void ProcessLeap(BossActionComponent &bossDef, TransformComponent &trans,
                     CharacterMovementComponent         &movement,
                     const JoltCharacterHandleComponent &joltHandle,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext &battle, float dt);

    // 踏み切りの瞬間に1回だけ呼ぶ。着地点と飛ぶ向きをここで確定させる。
    void BeginLeapFlight(BossActionComponent                &bossDef,
                         const DirectX::SimpleMath::Vector3 &takeoffPos,
                         const DirectX::SimpleMath::Vector3 &playerPos,
                         const BattleContext                &battle) const;

    // 中央を挟んで相手と反対側の着地点。高さは踏み切り地点に揃える。
    DirectX::SimpleMath::Vector3 AwayToCenterLandingPoint(const BossActionComponent          &bossDef,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos,
                                                          const BattleContext                &battle) const;

    // 相手の手前 attackStandOff の地点。遠ければ attackMaxRange で打ち切る。
    DirectX::SimpleMath::Vector3 TowardTargetLandingPoint(const BossActionComponent          &bossDef,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos) const;

    // 着地点をステージの内側へ引き戻す。どの着地点の決め方でも最後に通す。
    DirectX::SimpleMath::Vector3 ClampInsideArena(const DirectX::SimpleMath::Vector3 &point,
                                                  const BattleContext                &battle) const;

    // 飛行の進み具合 u (0=踏み切り, 1=着地) での位置。水平は等速、垂直は放物線。
    DirectX::SimpleMath::Vector3 LeapPositionAt(const BossActionComponent &bossDef, float u) const;

    // AIの意図(ActionID) を物理ステートへ翻訳する。
    BossActionState TranslateAction(ActionID actionId) const;

    // 技に入った瞬間だけ走る処理。タイマーの初期化と、進行方向の確定。
    // 跳躍系は、どの技かで着地点の決め方と調整値を選ぶので actionId も受け取る。
    //
    // 方向をここで1回だけ決めるのが、動く技が「避けられる技」である根拠。
    void OnEnterState(BossActionState next, ActionID actionId, BossActionComponent &bossDef,
                      const TransformComponent           &trans,
                      const DirectX::SimpleMath::Vector3 &playerPos) const;
};
