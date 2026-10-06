#pragma once
// ===========================================================================
// 概要: CharacterVirtual の移動・壁ずり・段差登り・重力・ジャンプ・ノックバックを計算する System。
//
// 責務:
//   - 移動の意思（desiredVelocity）と上書き要求から、水平・鉛直の速度を決める
//   - ExtendedUpdate で地形との干渉を解き、位置を Transform へ書き戻す
//   - presence プロキシ（Kinematic 剛体）を CharacterVirtual に追従させる
//   - ノックバックの加算と、ノックバック・硬直タイマーの消化
//
// 備考:
//   - L03_PrePhysics で、JoltStepSystem（L04）より前に動く。
//   - 接地状態は CharacterVirtual::GetGroundState() を正とする。
//     Facade のレイキャスト接地（剛体向け）はキャラクター移動には使わない。
//   - Transform へ書き戻すのは位置だけ。回転はゲーム層が ch->SetRotation で流す。
//   - overrideSpeed / use3DVelocity / jumpImpulse は読んだ時点で既定値へ戻す
//     （理由は CharacterMovementComponent.h）。
//   - ForEachWithID（直列）で回している。ループ内で共有の TempAllocator を使い、
//     BodyInterface へ MoveKinematic で書き込むので、並列版へ置き換えるとデータ競合になる。
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

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // Setup が生成した JoltCharacterHandle を使う。生成が後に回ると1フレーム空振りする。
    std::vector<const char *> GetRunAfter() const override { return {"JoltCharacterSetupSystem"}; }

    void Update(float dt) override;

private:
    // isTeleported が立っていれば、CharacterVirtual を Transform のワールド位置・回転へ移す
    void ApplyTeleportIfNeeded(TransformComponent &trans, JPH::CharacterVirtual *ch);

    /**
     * @brief 重力積分とジャンプ処理を行い、新たなY軸の速度を返す。
     * @param safeDt     経過時間（秒）。0 にならないよう丸めた値。
     * @param currentVy  今のY軸の速度（m/秒）。
     * @param config     キャラクターの設定。
     * @param movement   jumpImpulse を持つ移動の命令。jumpImpulse はここで消費する（1フレーム限りの命令）。
     * @param isGrounded 接地しているか。
     * @param gravityY   重力加速度のY成分（m/秒²）。
     * @return 新たなY軸の速度（m/秒）。
     */
    float CalculateVerticalVelocity(float safeDt, float currentVy, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement, bool isGrounded, float gravityY);

    /**
     * @brief 現在速度から目標速度へ Lerp で寄せ（加速/減速でレートを分ける）、新たな水平速度を返す。
     * @param safeDt   経過時間（秒）。0 にならないよう丸めた値。
     * @param config   キャラクターの設定（加速・減速のレート）。
     * @param movement 目標速度と overrideSpeed を持つ移動の命令。overrideSpeed はここで消費する（1フレーム限りの依頼）。
     * @return 新たな水平速度（m/秒）。
     */
    DirectX::SimpleMath::Vector3 CalculateHorizontalVelocity(float safeDt, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement);

    /**
     * @brief ExtendedUpdate で地形との干渉を解き、結果を Transform と presence プロキシへ書き戻す。
     * @param safeDt      CharacterVirtual の積分に使う時間（秒）。スローを反映した値。
     * @param moveDt      presence プロキシの追従（MoveKinematic）に使う実時間（秒）。
     *                    スロー中に safeDt を渡すと、物理の Step が実時間で積分してプロキシが飛んでいくため分けている。
     * @param joltManager 物理の管理。所有しない。
     * @param ch          更新するキャラクター。所有しない。
     * @param config      キャラクターの設定。
     * @param trans       結果を書き戻す Transform。
     * @param id          キャラクターのエンティティ。
     */
    void UpdatePhysicsAndSync(float safeDt, float moveDt, JoltPhysicsManager *joltManager, JPH::CharacterVirtual *ch, const JoltCharacterConfigComponent &config, TransformComponent &trans, CCL::ECS::EntityID id);
};