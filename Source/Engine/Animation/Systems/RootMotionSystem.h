#pragma once
// ===========================================================================
// 概要: アニメーションのルート移動量をキャラクターの移動速度に変換する System（L02_Update）
//
// 責務:
//   - ステートが applyRootMotion のとき、ルートモーションカーブ（あれば）かルートボーンの移動量から
//     このステップの前進量を求め、CharacterMovementComponent.rootMotionVelocity に書く
//   - 使わないステップでは出力を必ず無効にする
//
// 備考: ダッシュ等の個別の仕組みは知らない。外部が移動を制御中かどうかは
//       RootMotionComponent.externallyDriven（ゲーム側が書く）で受け取る
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"

class RootMotionSystem : public CCL::ECS::IfSystem<
    RootMotionSystem,
    CCL::ECS::Write<RootMotionComponent>,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Read<AnimatorComponent>,
    CCL::ECS::Read<AnimStateMachineComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<TimeStateComponent>,
    CCL::ECS::Read<ModelComponent>>
{
public:
    RootMotionSystem() : IfSystem("RootMotionSystem") {}

    // AnimControllerSystem が決めた遷移に当てるので、その後に走る
    // PlayerDashSystem が externallyDriven を書いてから読むので、その後にも走る
    std::vector<const char *> GetRunAfter() const override
    {
        return {"AnimControllerSystem", "PlayerDashSystem"};
    }

    void Update(float dt) override;

private:
    using Vector3 = DirectX::SimpleMath::Vector3;

    // このステップでルートモーションを使うか。使わないなら出力を消し、基準の時刻を捨ててから false を返す
    static bool ShouldApply(RootMotionComponent &rm, CharacterMovementComponent &movement,
                            const AnimatorComponent &animator, const AnimStateMachineComponent &fsm,
                            const Model *model);

    // クリップが切り替わった（頭から再生し直した）なら、基準の時刻を今に合わせて true を返す
    static bool ResetOnClipChange(RootMotionComponent &rm, const AnimatorComponent &animator);

    // ルートモーションカーブから、前回からの前進距離（m）を求め、基準の時刻を進める
    static float AdvanceCurveDistance(RootMotionComponent &rm, const AnimatorComponent &animator);

    // ルートボーンの、前回からの移動量（ローカル空間）を求め、基準の時刻を進める。ブレンド中は前後を補間する
    static Vector3 AdvanceBoneDelta(RootMotionComponent &rm, const AnimatorComponent &animator, Model *model,
                                    int animIndex);

    // ルートボーンの t0 から t1（秒）までの移動量
    // t1 < t0 はループで先頭へ戻ったとして、「末尾まで + 先頭から」でつなぐ
    static Vector3 ExtractBoneDelta(Model *model, int animIndex, int rootNodeIndex, float t0, float t1,
                                    float duration);

    // キャラの正面を水平にした向き（長さ 1）
    static Vector3 FlatForward(const TransformComponent &trans);

    // 向き forward・速さ speed（m/秒）の移動を出力に書く。speed が負なら後退
    static void WriteOutput(CharacterMovementComponent &movement, const Vector3 &forward, float speed);

    // 出力を無効にする
    static void ClearOutput(CharacterMovementComponent &movement);
};