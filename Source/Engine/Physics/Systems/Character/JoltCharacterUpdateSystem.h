#pragma once
// ===========================================================================
// 概要: CharacterVirtual を、移動の依頼に合わせて1フレーム分動かす System。
//
// 責務:
//   - 移動の依頼（CharacterMovementComponent）から、水平・上下の速度を決める
//   - ノックバックを足し、ノックバックと硬直のタイマーを減らす
//   - ExtendedUpdate で地形との干渉を解き、位置を Transform へ書き戻す
//   - 他者から見えるプロキシ剛体を、CharacterVirtual に追従させる
//
// 備考:
//   依頼が重なったときの優先順は CharacterMovementComponent.h の表のとおり。
//   L03_PrePhysics で、物理の Step（L04）より前に動く。
//   接地は CharacterVirtual::GetGroundState() を正とする。
//   Transform へ書き戻すのは位置だけ。向きはゲーム側が SetCharacterRotation で書く。
//   直列で回す。共有の TempAllocator と BodyInterface を使うので、並列にするとデータ競合になる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Time/Components/TimeState.h"

class JoltPhysicsManager;

class JoltCharacterUpdateSystem
    : public CCL::ECS::IfSystem<JoltCharacterUpdateSystem,
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

    /**
     * @brief 重力とジャンプから、新しい上下の速度を返す。
     * @param safeDt     経過時間（秒）。CharacterStepDt で丸めた値。
     * @param currentVy  今の上下の速度（m/秒）。
     * @param config     キャラクターの設定。
     * @param movement   jumpImpulse を持つ移動の依頼。jumpImpulse はここで消費する。
     * @param isGrounded 接地しているか。
     * @param gravityY   重力加速度の Y 成分（m/秒²）。
     * @return 新しい上下の速度（m/秒）。
     */
    float CalculateVerticalVelocity(float safeDt, float currentVy, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement, bool isGrounded, float gravityY);

    /**
     * @brief 目標の水平速度へ慣性付きで寄せ、新しい水平速度を返す。
     * @param safeDt   経過時間（秒）。CharacterStepDt で丸めた値。
     * @param config   キャラクターの設定（歩く速さ・加減速）。
     * @param movement 移動の依頼。overrideSpeed はここで消費する。
     * @return 新しい水平速度（m/秒）。
     */
    DirectX::SimpleMath::Vector3 CalculateHorizontalVelocity(float safeDt, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement);

    // ノックバックを水平速度へ足し、ノックバックと硬直のタイマーを減らす
    // KnockbackComponent を持たないキャラでは何もしない
    void ApplyKnockback(CCL::ECS::EntityID id, float safeDt, DirectX::SimpleMath::Vector3 &horizontalVel);

    /**
     * @brief 移動の予約を1フレーム進め、その速度を use3DVelocity で依頼する。
     *
     * MoveTo / Hold は居るべき位置へ届く速度、MoveInDirection は決めた向きと速さ。
     * 終わったフレームは終点ちょうどへ運び、次のフレームに速度を 0 にする。
     *
     * @param movement 予約を持つ移動の依頼。
     * @param ch       位置を読むキャラクター。
     * @param localDt  このキャラの時計の dt（秒）。予約の経過時間に使う。
     */
    void ApplyMoveReservation(CharacterMovementComponent &movement, const JPH::CharacterVirtual *ch, float localDt);

    /**
     * @brief ExtendedUpdate で地形との干渉を解き、結果を Transform とプロキシ剛体へ書き戻す。
     * @param safeDt      CharacterVirtual の積分に使う時間（秒）。スローを反映した値。
     * @param moveDt      プロキシ剛体の追従（MoveKinematic）に使う実時間（秒）。
     *                    スロー中に safeDt を渡すと、物理の Step が実時間で積分してプロキシが飛んでいく。
     * @param joltManager 物理の管理。所有しない。
     * @param ch          更新するキャラクター。所有しない。
     * @param config      キャラクターの設定。
     * @param trans       結果を書き戻す Transform。
     * @param id          キャラクターのエンティティ。
     */
    void UpdatePhysicsAndSync(float safeDt, float moveDt, JoltPhysicsManager *joltManager, JPH::CharacterVirtual *ch, const JoltCharacterConfigComponent &config, TransformComponent &trans, CCL::ECS::EntityID id);
};
