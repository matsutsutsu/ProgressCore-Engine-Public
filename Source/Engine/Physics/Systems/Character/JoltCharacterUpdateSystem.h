#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Time/TimeState.h"

// ===================================================================================
// ファイル名: JoltCharacterUpdateSystem.h
// 役割:
// 仮想キャラクター（CharacterVirtual）の移動・壁ずり・段差登り・重力・ジャンプを
// 計算する専用システム。
//
// 【アーキテクチャ仕様】
// - 実行フェーズ: PrePhysics (JoltStepSystem が走る前に終わらせるのが公式の絶対ルール)
// - 対象: CharacterVirtual / Input / Config をすべて持つ Entity。
//
// 【接地判定の唯一の真実】
// - 接地状態は CharacterVirtual::GetGroundState() を権威とする。
//   Facade のレイキャスト接地（剛体対象）はキャラクター移動には使わない。
//
// 【使い方・ルール】
// - 「意思（desiredVelocity）」と上書き要求を受け取り、地形との干渉を計算する。
// - 計算結果の「位置(Position)」だけを Transform に書き戻す。回転(Rotation)は同期しない。
// - isTeleported フラグが立っている場合は、計算前にワープ処理を優先して行う。
//
// 【1フレーム限りの要求はここで消費する】
// - overrideSpeed / use3DVelocity / jumpImpulse は読んだ時点で既定値へ戻す。
//   要求側は効かせたいフレームに書くだけでよく、解除処理を持たない。
//   詳しい理由は CharacterMovementComponent.h を参照。
//
// 【並列化に関する重大な注意】
// - 本システムは ForEachWithID（シングルスレッド）で実行している。
//   ループ内で BodyInterface（非ロック版）へ書き込みを行うため、
//   ForEachWithIDParallel へ安易に置き換えると並列書き込みでクラッシュする。
// ===================================================================================

// 前方宣言
class JoltPhysicsManager;

class JoltCharacterUpdateSystem
    : public CCL::ECS::IfSystem<JoltCharacterUpdateSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<TimeStateComponent>> {
public:
    JoltCharacterUpdateSystem() : IfSystem("JoltCharacterUpdateSystem") {}
    virtual ~JoltCharacterUpdateSystem() = default;


    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // Setup が生成した JoltCharacterHandle を使う。生成が後に回ると1フレーム空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltCharacterSetupSystem"}; }

    void Update(float dt) override;

    // 強制ワープフラグが立っていればJoltを即座に移動させる
    void ApplyTeleportIfNeeded(TransformComponent &trans, JPH::CharacterVirtual *ch);

    // 重力積分とジャンプ処理を行い、新たなY軸の速度を返す。
    //
    // jumpImpulse をここで消費する（1フレーム限りの命令）。
    float CalculateVerticalVelocity(float safeDt, float currentVy, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement, bool isGrounded, float gravityY);

    // 慣性の引き継ぎ(Exponential Decay)を計算し、新たな水平速度ベクトルを返す。
    //
    // overrideSpeed をここで消費する（1フレーム限りの依頼）。
    DirectX::SimpleMath::Vector3 CalculateHorizontalVelocity(float safeDt, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement);

    // 地形干渉計算を実行し、結果をTransformやKinematic剛体へ書き戻す
    void UpdatePhysicsAndSync(float safeDt, float moveDt, JoltPhysicsManager *joltManager, JPH::CharacterVirtual *ch, const JoltCharacterConfigComponent &config, TransformComponent &trans, CCL::ECS::EntityID id);
};