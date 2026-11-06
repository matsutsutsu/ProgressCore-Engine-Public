#include "DroneMovementSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h"
#include "Game/Logics/AI/Drone/Motion/DroneMotion.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace Motion = CCL::AI::Motion;

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

    // ============================================================
    // デバッグ UI の部品
    // ============================================================

    // 直前の項目にマウスを乗せている間だけ説明を出す
    void HoverTooltip(const char *text)
    {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    }
} // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void DroneMovementSystem::Initialize()
{
    if (m_world->HasResource<Motion::MotionTuning>()) return;
    m_world->AddResource<Motion::MotionTuning>();
}



void DroneMovementSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. 動き方が使う World の値を写し取る
    // 2. ホバーの位相を進める
    // 3. 状態ごとに速度と姿勢を決める
    // 4. 速度を位置へ積分する
    // 5. 床より下へ潜らせない
    // 6. 姿勢を正規化して確定する

    // --- World の値 ---

    // 並列ループの中で World を引かないよう、ループの前に写し取る
    Motion::BeginMotionFrame(*m_world);

    ForEachParallel([this](TransformComponent &trans, DroneComponent &drone,
                           const TimeStateComponent &time) {
        float dt = time.localDt; // 各機の時間。ヒットストップ中は 0 になり止まる

        // --- ホバーの位相 ---

        // 2π で巻き戻す。増え続けると float の刻みが粗くなり、長時間プレイで揺れがカクつく
        drone.hoverTimeOffset += dt;
        if (drone.hoverTimeOffset >= kHoverPhasePeriod)
            drone.hoverTimeOffset -= kHoverPhasePeriod;

        // --- 状態ごとの速度と姿勢 ---

        // 剛体追従の「追いついたか」は突撃1回ごとに数え直す
        if (drone.currentState != DroneState::FireCharge) drone.rigidLocked = false;

        // 各ステートは速度と姿勢を決めるだけ。積分と床はこの後で共通に行う
        // （離脱だけはこの System の ProcessReturning、他は DroneMotion）
        switch (drone.currentState)
        {
        case DroneState::MoveToTarget:
            Motion::MoveToSlot(trans, drone, dt);
            break;
        case DroneState::Idle:
            Motion::HoldSlot(trans, drone, dt);
            break;
        case DroneState::FireCharge:
            Motion::MoveCharge(trans, drone, dt);
            break;
        case DroneState::Returning:
            ProcessReturning(trans, drone, dt);
            break;
        }

        // --- 積分 ---

        trans.position = trans.position + drone.currentVelocity * dt;

        // --- 床 ---

        // 地面へ潜らない保証はここ1か所だけが持つ
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
        Motion::TurnToward(trans, look, /*includePitch*/ true, kBreakaway.turnRate, dt);
    }
}



// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void DroneMovementSystem::OnGui()
{
    if (!m_world->HasResource<Motion::MotionTuning>()) return;
    auto &tuning = m_world->GetResource<Motion::MotionTuning>();

    ImGui::DragFloat("持ち場へ寄る速さの上限", &tuning.moveSpeedScale,1.0f, 1.0f, 6.0f, "x%.1f");
    HoverTooltip("強いバネで出せる速さ（moveSpeed 倍）。生成直後の整列・陣形の切り替え・突撃後の帰還が速くなる");
    ImGui::DragFloat("強いバネに切り替える距離", &tuning.farFromSlot,3.0f, 0.0f, 200.0f, "%.1f m");
    HoverTooltip("待機中でも持ち場からこれより遠いと強いバネで寄る。0 にすると常に強いバネになり、ホバーの揺れが消える");
    ImGui::DragFloat("剛体に切り替える距離", &tuning.rigidCatchUp,3.0f, 0.1f, 200.0f, "%.1f m");
    HoverTooltip("データ陣形の突撃で、持ち場にこれより近づいたら形に固定する。大きいほど早く固定されるが、固定の瞬間に跳んで見える");

    if (ImGui::Button("既定値に戻す", ImVec2(140, 24)))
        tuning = Motion::MotionTuning{};
}



// JoltPushSystem（L03_PushToPhysics）が物理へ渡す前に位置を確定する
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);
