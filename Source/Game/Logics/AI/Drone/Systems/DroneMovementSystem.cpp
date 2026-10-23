#include "DroneMovementSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h"
#include "Game/Logics/AI/Drone/Motion/DroneMotion.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

using namespace CCL::ECS;
using namespace CCL::AI::Motion;
using namespace DirectX::SimpleMath;

namespace
{
    // ============================================================
    // 調整値（この System に残る状態）
    // ============================================================

    // 離脱（Returning）
    struct BreakawayTuning
    {
        float drag;      // 減速率 [1/s]
        float lift;      // 上方向へ加える加速度 [m/s²]
        float fastSpeed; // 浮き上がりを効かせ始める速さ [m/s]
        float turnRate;  // 機首を向け直す速さ [1/s]。制御を取り戻しつつある表現として、他の状態より鈍い
    };
    constexpr BreakawayTuning kBreakaway{.drag = 2.5f, .lift = 18.0f, .fastSpeed = 40.0f, .turnRate = 8.0f};
} // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void DroneMovementSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. 調整値を写し取り、動き方にフレームの始まりを伝える
    // 2. ホバーの位相を進める
    // 3. 状態ごとに速度と姿勢を決める
    // 4. 速度を位置へ積分する
    // 5. 床より下へ潜らせない
    // 6. 姿勢を正規化して確定する

    // --- 調整値 ---

    // 並列ループの中で World を引かないよう、ループの前に写し取る
    if (!m_world->HasResource<DroneShiverTuning>()) m_world->AddResource<DroneShiverTuning>();
    m_motion.shiver = m_world->GetResource<DroneShiverTuning>();

    BeginMotionFrame(*m_world);

    ForEachParallel([this](TransformComponent &trans, DroneComponent &drone,
                           const TimeStateComponent &time) {
        float dt = time.localDt; // 各機の時間。ヒットストップ中は 0 になり止まる

        // --- ホバーの位相 ---

        // 2π で巻き戻す。増え続けると float の刻みが粗くなり、長時間プレイで揺れがカクつく
        drone.hoverTimeOffset += dt;
        if (drone.hoverTimeOffset >= kHoverPhasePeriod)
            drone.hoverTimeOffset -= kHoverPhasePeriod;

        // --- 状態ごとの速度と姿勢 ---

        // 各ステートは速度と姿勢を決めるだけ。積分と床はこの後で共通に行う
        switch (drone.currentState)
        {
        case DroneState::MoveToTarget:
            MoveToSlot(trans, drone, dt, m_motion);
            break;
        case DroneState::Idle:
            HoldSlot(trans, drone, dt, m_motion);
            break;
        case DroneState::FireCharge:
            MoveCharge(trans, drone, dt, m_motion);
            break;
        case DroneState::Returning:
            ProcessReturning(trans, drone, dt);
            break;
        }

        // --- 積分 ---

        trans.position = trans.position + drone.currentVelocity * dt;

        // --- 床 ---

        // 地面へ潜らない保証はここ1か所だけが持つ。
        // 離脱は持ち場を経由せず速度で動き、剛体追従は位置へ直接書くので、陣形側で目標を丸めても止まらない
        const float floorY = kDroneGroundY + kDroneGroundClearance;
        if (trans.position.y < floorY)
        {
            trans.position.y = floorY;

            // 下向きの速度を残すと、地面に張り付いたまま滑る
            if (drone.currentVelocity.y < 0.0f) drone.currentVelocity.y = 0.0f;
        }

        // --- 姿勢の確定 ---

        // Slerp を毎フレーム重ねると誤差で長さが1からずれるので、毎フレーム正規化する
        DirectX::XMVECTOR quatVec = DirectX::XMLoadFloat4(&trans.rotation);
        quatVec                   = DirectX::XMQuaternionNormalize(quatVec);
        DirectX::XMStoreFloat4(&trans.rotation, quatVec);

        trans.isDirty = true;
    });
}

// ---------------------------------------------------------------------------
// 状態ごとの移動（どの技にも共通のもの）
// ---------------------------------------------------------------------------

void DroneMovementSystem::ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 処理フロー:
    // 1. 速度を抜く
    // 2. 遅くなるほど上へ浮かせる
    // 3. 機首を進行方向へ向ける

    // --- 減速 ---

    // 減速しきらないと、次の MoveToTarget で高速のまま急に向きを変えて帰る絵になる
    float drag = 1.0f - kBreakaway.drag * dt;
    if (drag < 0.0f) drag = 0.0f;
    drone.currentVelocity = drone.currentVelocity * drag;

    // --- 浮き上がり ---

    // 遅くなるほど強く効かせる。全速のまま上へ押すと、突き抜けた瞬間に軌道が跳ね上がる
    // 減速しきって漂う間は当たらないので、プレイヤーが反撃できる間になる
    const float speed = drone.currentVelocity.Length();
    const float fast  = (std::min)(speed / kBreakaway.fastSpeed, 1.0f);

    drone.currentVelocity += Vector3::UnitY * (kBreakaway.lift * (1.0f - fast) * dt);

    // --- 機首 ---

    // 目標ではなく進行方向へ機首を向ける
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.001f)
    {
        look.Normalize();
        TurnToward(trans, look, /*includePitch*/ true, kBreakaway.turnRate, dt);
    }
}

// JoltPushSystem（L03_PushToPhysics）が物理へ渡す前に位置を確定する
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);
