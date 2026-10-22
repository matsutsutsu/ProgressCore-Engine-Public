#include "DroneMovementSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h"
#include "Game/Logics/AI/Drone/Motion/ChargeMotions.h"
#include "Game/Logics/AI/Drone/Motion/DroneMotionCommon.h"
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

    // 離脱（Returning）。
    struct BreakawayTuning
    {
        float drag;      // 減速率 [1/s]
        float lift;      // 上方向へ加える加速度 [m/s²]
        float fastSpeed; // 浮き上がりを効かせ始める速さ [m/s]
        float turnRate;  // 機首を向け直す速さ [1/s]
    };

    // 機首の向け直しを他の状態より鈍くしているのは、「制御を取り戻しつつある」表現。
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

    // 震えの調整値を写し取る。無ければ既定値で作る（GUI を開かなくても同じ見た目になる）。
    if (!m_world->HasResource<DroneShiverTuning>()) m_world->AddResource<DroneShiverTuning>();
    m_motion.shiver = m_world->GetResource<DroneShiverTuning>();

    // 動き方が自分だけで使う調整値は、動き方が自分で写し取る（IChargeMotion::BeginFrame）。
    // 並列ループの中で resource を引かないため、ループの前に済ませる。
    BeginChargeMotionsFrame(*m_world);

    ForEachParallel([this](TransformComponent &trans, DroneComponent &drone,
                           const TimeStateComponent &time) {
        float dt = time.localDt; // 各機の時間。ヒットストップ中は 0 になり止まる

        // --- ホバーの位相 ---

        // 位相を進めて 2π で巻き戻す。
        // 放置すると単調増加して float の刻みが粗くなり、長時間プレイで揺れが量子化する。
        drone.hoverTimeOffset += dt;
        if (drone.hoverTimeOffset >= kHoverPhasePeriod)
            drone.hoverTimeOffset -= kHoverPhasePeriod;

        // --- 状態ごとの速度と姿勢 ---

        // 各ステートは drone.currentVelocity / rotation を更新するだけ。
        // 位置への積分と床の処理は、この後で全ステート共通に1回だけ行う。
        switch (drone.currentState)
        {
        case DroneState::MoveToTarget:
            MoveToSlot(trans, drone, dt, m_motion);
            break;
        case DroneState::Idle:
            HoldSlot(trans, drone, dt, m_motion);
            break;
        case DroneState::FireCharge:
            // 突撃中の動きは、技が選んだ動き方に任せる。
            GetChargeMotion(drone.chargeMode).Move(trans, drone, dt, m_motion);
            break;
        case DroneState::Returning:
            ProcessReturning(trans, drone, dt);
            break;
        }

        // --- 積分 ---

        trans.position = trans.position + drone.currentVelocity * dt;

        // --- 床 ---

        // 地面へ潜らない保証はここ1か所だけが持つ。
        // 技ごとに目標を丸めても、離脱は持ち場を経由せず速度で動き、
        // 剛体追従は trans.position へ直接書くので、陣形側のクランプでは止まらない。

        // 小隊の kSquadFloorY は「地上2mを飛ぶ」という飛び方の規則で、この床とは別物。
        const float floorY = kDroneGroundY + kDroneGroundClearance;
        if (trans.position.y < floorY)
        {
            trans.position.y = floorY;

            // 下向きの速度を残すと次のフレームも床へ押し付け、地面に張り付いたまま滑る。
            if (drone.currentVelocity.y < 0.0f) drone.currentVelocity.y = 0.0f;
        }

        // --- 姿勢の確定 ---

        // Slerp と回転の合成を毎フレーム重ねるので、誤差で長さが1からずれていく。
        // 確定の前に毎フレーム正規化する。
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

    // まず速度を抜く。
    // 減速しきらないと次の MoveToTarget が高速のまま隊列へ引き戻し、
    // 飛んできた機がその速さのまま急に向きを変えて帰る絵になる。
    float drag = 1.0f - kBreakaway.drag * dt;
    if (drag < 0.0f) drag = 0.0f;
    drone.currentVelocity = drone.currentVelocity * drag;

    // --- 浮き上がり ---

    // 浮き上がりは「減速してから」効かせる。
    // 全速のまま上向きの力を加えると、突き抜けた瞬間に軌道が跳ね上がる。

    // 減速しきった後の「漂っている時間」が、突撃と帰投のあいだの間になる。
    // FireCharge ではないので無害で、プレイヤーが反撃してよい時間でもある。
    const float speed = drone.currentVelocity.Length();
    const float fast  = (std::min)(speed / kBreakaway.fastSpeed, 1.0f);

    drone.currentVelocity += Vector3::UnitY * (kBreakaway.lift * (1.0f - fast) * dt);

    // --- 機首 ---

    // 目標ではなく速度の向きを向く（航空機と同じく進行方向へ機首を向ける）。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.001f)
    {
        look.Normalize();
        TurnToward(trans, look, /*includePitch*/ true, kBreakaway.turnRate, dt);
    }
}

// JoltPushSystem（L03_PushToPhysics）が物理へ渡す前に位置を確定する。
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);
