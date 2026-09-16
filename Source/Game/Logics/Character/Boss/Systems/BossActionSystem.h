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

    // 突進斬り。溜め → 固定方向へ踏み込み → 硬直、を actionTimer で切り替える。
    //
    // 踏み込みに入ったら旋回しない。最後まで追尾すると避けられない技になるため。
    void ProcessDashSlash(BossActionComponent &bossDef, TransformComponent &trans,
                          CharacterMovementComponent         &movement,
                          const JoltCharacterHandleComponent &joltHandle,
                          const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 跳躍斬り。溜め → 放物線で跳ぶ → 着地硬直。
    //   滞空中は use3DVelocity で速度を直接与える。重力に任せると滞空時間が
    //   キャラ設定に左右され、着地の瞬間をアニメの斬撃へ合わせられない。
    void ProcessJumpAttack(BossActionComponent &bossDef, TransformComponent &trans,
                           CharacterMovementComponent         &movement,
                           const JoltCharacterHandleComponent &joltHandle,
                           const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 後方ステップ。減速しながら後ろへ抜け、そのあと硬直する。
    //
    // 退きながらプレイヤーを向き続ける。背を向けると「逃げた」に見えてしまい、
    // 間合いを切って仕切り直す技だと読めなくなる。
    void ProcessBackStep(BossActionComponent &bossDef, TransformComponent &trans,
                         CharacterMovementComponent         &movement,
                         const JoltCharacterHandleComponent &joltHandle,
                         const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // AIの意図(ActionID) を物理ステートへ翻訳する。
    BossActionState TranslateAction(ActionID actionId) const;

    // 技に入った瞬間だけ走る処理。タイマーの初期化と、進行方向・着地点の確定。
    //
    // 方向をここで1回だけ決めるのが、突進と跳躍が「避けられる技」である根拠。
    void OnEnterState(BossActionState next, BossActionComponent &bossDef,
                      const TransformComponent           &trans,
                      const DirectX::SimpleMath::Vector3 &playerPos) const;
};
