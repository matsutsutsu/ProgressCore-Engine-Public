#pragma once
// ===========================================================================
// 概要: CharacterVirtual を、移動の依頼に合わせて1フレーム分動かす System
//
// 責務:
//   - 移動の依頼（CharacterMovementComponent）から、水平・上下の速度を決める
//   - ノックバックを足し、ノックバックと硬直のタイマーを減らす
//   - ExtendedUpdate で地形との干渉を解き、位置を Transform へ書き戻す
//   - 他者から見えるプロキシ剛体を、CharacterVirtual に追従させる
//
// 備考: 依頼が重なったときの優先順は CharacterMovementComponent.h の表のとおり
//       L03_PrePhysics で、物理の Step（L04）より前に動く。接地は CharacterVirtual::GetGroundState() を正とする
//       Transform へ書き戻すのは位置だけで、向きはゲーム側が SetCharacterRotation で書く
//       共有の TempAllocator と BodyInterface を使うので、並列にせず直列で回す
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Time/Components/TimeState.h"

class JoltPhysicsManager;

class JoltCharacterUpdateSystem : public CCL::ECS::IfSystem<
    JoltCharacterUpdateSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    JoltCharacterUpdateSystem() : IfSystem("JoltCharacterUpdateSystem") {}
    virtual ~JoltCharacterUpdateSystem() = default;

    // Setup が作った CharacterVirtual を使うので、Setup の後に走る
    // 生成が後に回ると、生成したフレームを1回空振りする
    std::vector<const char *> GetRunAfter() const override { return {"JoltCharacterSetupSystem"}; }

    // テンプレート引数以外に GetComponent で読む型の申告（プロキシ剛体の Handle）
    // テンプレート引数に足すと ForEach の絞り込みになり、持たないキャラが動かなくなる
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // テンプレート引数以外に GetComponent で書く型の申告（KnockbackComponent）
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    void Update(float dt) override;

private:
    // isTeleported が立っていれば、CharacterVirtual を Transform のワールド位置・回転へ移す
    void ApplyTeleportIfNeeded(TransformComponent &trans, JPH::CharacterVirtual *ch);

    // 重力とジャンプから、新しい上下の速度（m/秒）を返す。jumpImpulse はここで消費する
    float CalculateVerticalVelocity(float safeDt, float currentVy, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement, bool isGrounded, float gravityY);

    // 目標の水平速度へ慣性付きで寄せ、新しい水平速度（m/秒）を返す。overrideSpeed はここで消費する
    DirectX::SimpleMath::Vector3 CalculateHorizontalVelocity(float safeDt, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement);

    // ノックバックを水平速度へ足し、ノックバックと硬直のタイマーを減らす
    // KnockbackComponent を持たないキャラでは何もしない
    void ApplyKnockback(CCL::ECS::EntityID id, float safeDt, DirectX::SimpleMath::Vector3 &horizontalVel);

    // 移動の予約を1フレーム進め、その速度を use3DVelocity で依頼する
    // MoveTo / Hold は届くべき位置への速度、MoveInDirection は決めた向きと速さ。終わったら終点へ運び、次で 0 にする
    void ApplyMoveReservation(CharacterMovementComponent &movement, const JPH::CharacterVirtual *ch, float localDt);

    // ExtendedUpdate で地形との干渉を解き、結果を Transform とプロキシ剛体へ書き戻す
    // safeDt はスローを反映した積分の時間、moveDt はプロキシの追従に使う実時間
    // スロー中にプロキシへ safeDt を渡すと、物理の Step が実時間で積分してプロキシが飛んでいく
    void UpdatePhysicsAndSync(float safeDt, float moveDt, JoltPhysicsManager *joltManager, JPH::CharacterVirtual *ch, const JoltCharacterConfigComponent &config, TransformComponent &trans, CCL::ECS::EntityID id);
};
