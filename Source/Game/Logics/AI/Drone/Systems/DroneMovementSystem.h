#pragma once
// ===========================================================================
// 概要: ドローンを目標座標へ移動・回転させる System（L03_PrePhysics）。
//
// 責務:
//   - currentState ごとに速度（currentVelocity）と姿勢を決める
//   - 速度を位置へ積分し、床より下へ潜らせない
//
// 備考:
//   状態はボスの指示で動く3つ（Idle / MoveToTarget / FireCharge）と、個体が自分で
//   入る2つ（Stunned / Returning）。
//
//   突撃中（FireCharge）の動きは、技が選んだ動き方（drone.chargeMode → IChargeMotion）に任せる。
//   技に固有の動きをここへ書かないため（動き方を足しても、この System は変わらない）。
//
//   Idle と MoveToTarget の動きは、動き方クラスも使うので DroneMotionCommon に置いてある。
//   ここに残すのは、どの技にも共通の Stunned / Returning と、積分・床・姿勢の確定。
//
//   ホバーの揺れや予告の震えなどの演出は上乗せで表現し、ステートにはしない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Motion/IChargeMotion.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"

class DroneMovementSystem : public CCL::ECS::IfSystem<
                                DroneMovementSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<DroneComponent>,
                                CCL::ECS::Read<TimeStateComponent>>
{
public:
    DroneMovementSystem() : IfSystem("DroneMovementSystem") {}

    void Update(float dt) override;

private:
    /**
     * @brief 硬直。吹き飛ばし速度で漂いながら減衰し、制御を失った姿勢を見せる。
     * @note タイマー管理は DroneBrainSystem 側で、ここは動きだけ。
     */
    void ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt);

    /**
     * @brief 突き抜けた後、上方へ抜けながら減速する離脱カーブ。
     *
     * その場で急旋回させると突撃の勢いが嘘になるので、一度抜けさせてから MoveToTarget に復帰を任せる。
     */
    void ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt);

    // 動き方へ渡す調整値。Update の先頭で World リソースから写し取る（並列ループの中で resource を引かないため）。
    CCL::AI::Motion::MotionContext m_motion{};
};
