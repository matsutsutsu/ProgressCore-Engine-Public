#include "PlayerRushCameraSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Camera/CameraEvents.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Game/Logics/Camera/PlayerCameraComponent.h"
#include "Game/Logics/Camera/PlayerCameraLookup.h"
#include "Engine/Time/TimeService.h"
#include "Game/Core/GameEvents.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    const uint32_t kSource   = CCL::Utils::HashString("RushCamera"); // 構図の上書きスロットの発生源
    constexpr int  kPriority = 10;                                    // 他の演出と重なったときの順（大きいほど後）

    // ばねの角振動数 = kSettle / blendTime
    // 減衰比 1 のとき、blendTime 秒で目標の約95%に届く
    constexpr float kSettle = 4.7f;

    constexpr float kMinZeta    = 0.2f;  // 減衰比の下限。これより小さいと何度も揺れ返して酔う
    constexpr float kMaxStepArg = 0.25f; // 1刻みの omega*dt の上限。超えると発散する
    constexpr int   kMaxSubstep = 8;     // 刻みの上限。dt が大きいフレームでも回しすぎない
}



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

std::vector<TypeID> PlayerRushCameraSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<PlayerCameraComponent>::ID()); // 構図の上書きスロット
    return types;
}



std::vector<TypeID> PlayerRushCameraSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(TypeInfo<VirtualCamera>::ID()); // 画角パンチの基準にする今の画角
    return types;
}



void PlayerRushCameraSystem::Update(float dt)
{
    // 処理フロー:
    // 1. ラッシュの開始と斬りの瞬間を見つけ、揺れと画角パンチを要求する
    // 2. 今のフェーズの構図へ寄せる（終了後は効きを落とす）
    // 3. カメラの構図の上書きスロットへ書く（効きが 0 になったら手放す）

    // 描画ステージは実時間で回る。ポーズ中だけ dt を 0 にして構図を止める
    dt = Time::PresentationDt(*m_world, dt);

    ForEachWithID([&](EntityID                   id,
                      PlayerRushCameraComponent &cam,
                      const PlayerRushComponent &rush) {
        const EntityID cameraID = FindPlayerCamera(*m_world, id);

        // ---------------------------------------------------------
        // 1. 開始・斬りの瞬間
        // ---------------------------------------------------------

        if (rush.isActive && !cam.wasActive)
        {
            // 前の演出が戻りきっていれば、ばねを止まった状態から始める
            // 戻る途中で次が始まったときは、構図が跳ばないよう今の値のまま続ける
            if (cam.weight <= 0.0f)
            {
                cam.current  = CameraFramingParams{};
                cam.velocity = CameraFramingParams{};
            }
            cam.weight  = 1.0f;
            cam.yawSign = cam.yawFollowsEvade ? DecideYawSign(rush, cameraID) : 1.0f;

            if (cam.startShake > 0.0f)
            {
                m_world->GetEventBus().Publish(CameraShakeRequestEvent{cam.startShake});
            }
        }

        // 斬りの瞬間は、Attack 中に attackFired が立ったとき
        // Attack の間だけ見るのは、前のラッシュで立ったままの値を拾わないため
        const bool attackFiredNow = rush.isActive && rush.phase == RushPhase::Attack && rush.attackFired;
        if (attackFiredNow && !cam.wasAttackFired)
        {
            PunchStrike(cam, id, cameraID);
        }

        cam.wasActive      = rush.isActive;
        cam.wasAttackFired = attackFiredNow;

        // ---------------------------------------------------------
        // 2. 構図を寄せる
        // ---------------------------------------------------------

        if (rush.isActive)
        {
            const RushCameraShot     &shot = SelectShot(cam, rush.phase);
            // 注視点はラッシュの相手へ寄せる。相手が死ぬと PlayerRushSystem が無効にする
            const CameraFramingParams goal = ToFraming(shot, cam.yawSign, rush.closeInTarget);
            SpringToward(cam.current, cam.velocity, goal, shot.blendTime, shot.bounce, dt);
        }
        else if (cam.weight > 0.0f)
        {
            // 終了後は構図を保ったまま、効きだけを線形に落とす
            // 端の丸めは、書き込むときの SmoothStep で行う
            const float releaseTime = (std::max)(cam.releaseTime, 0.01f);
            cam.weight              = (std::max)(0.0f, cam.weight - dt / releaseTime);
        }

        // ---------------------------------------------------------
        // 3. カメラへ書く
        // ---------------------------------------------------------

        auto *tps = (cameraID != InvalidEntityID) ? m_world->GetComponent<PlayerCameraComponent>(cameraID) : nullptr;
        if (!tps) return;

        if (cam.weight <= 0.0f)
        {
            tps->framingOverrides.Release(kSource);
            return;
        }

        if (auto *slot = tps->framingOverrides.Acquire(kSource, kPriority))
        {
            slot->weight = CCL::Math::SmoothStep(cam.weight);
            slot->params = cam.current;
        }
    });
}



// ---------------------------------------------------------------------------
// 構図
// ---------------------------------------------------------------------------

const RushCameraShot &PlayerRushCameraSystem::SelectShot(const PlayerRushCameraComponent &cam, RushPhase phase) const
{
    switch (phase)
    {
    case RushPhase::Locked:
        return cam.locked;
    case RushPhase::Ready:
        return cam.ready;
    case RushPhase::Attack:
        return cam.attack;
    case RushPhase::Flurry:
        return cam.flurry;
    }
    return cam.locked;
}



CameraFramingParams PlayerRushCameraSystem::ToFraming(const RushCameraShot &shot, float yawSign, EntityID target) const
{
    // ヨーと傾きには、開始時に決めた左右を掛ける
    // 注視先がいなければ、寄せる割合も 0 にする
    CameraFramingParams goal{};
    goal.distanceOffset = shot.distance;
    goal.fovOffset      = shot.fov;
    goal.pitchOffset    = shot.pitch;
    goal.yawOffset      = shot.yaw * yawSign;
    goal.roll           = shot.roll * yawSign;
    goal.followBoost    = shot.followBoost;
    goal.focus          = (target != InvalidEntityID) ? shot.focus : 0.0f;
    goal.focusTarget    = target;
    return goal;
}



void PlayerRushCameraSystem::SpringToward(CameraFramingParams &current, CameraFramingParams &velocity,
                                    const CameraFramingParams &goal, float blendTime, float bounce, float dt) const
{
    // ばねを使うのは、行き過ぎて戻る反動を出すため
    // 指数のイーズ（Damp）は必ず目標の手前で止まるので、反動が出せない
    const float omega = kSettle / (std::max)(blendTime, 0.01f);
    const float zeta  = std::clamp(1.0f - bounce, kMinZeta, 1.0f);

    SpringStep(current.distanceOffset, velocity.distanceOffset, goal.distanceOffset, omega, zeta, dt);
    SpringStep(current.fovOffset, velocity.fovOffset, goal.fovOffset, omega, zeta, dt);
    SpringStep(current.pitchOffset, velocity.pitchOffset, goal.pitchOffset, omega, zeta, dt);
    SpringStep(current.yawOffset, velocity.yawOffset, goal.yawOffset, omega, zeta, dt);
    SpringStep(current.roll, velocity.roll, goal.roll, omega, zeta, dt);
    SpringStep(current.followBoost, velocity.followBoost, goal.followBoost, omega, zeta, dt);
    SpringStep(current.focus, velocity.focus, goal.focus, omega, zeta, dt);

    // 行き過ぎても、寄せる割合と追従の足し込みが範囲から出ないようにする
    current.focus       = std::clamp(current.focus, 0.0f, 1.0f);
    current.followBoost = (std::max)(current.followBoost, 0.0f);

    // 寄せ先がいなくなっても前の寄せ先を残し、割合だけを 0 へ落とす
    // 寄せ先を先に消すと、注視点がその場で跳ぶ
    if (goal.focusTarget != InvalidEntityID)
    {
        current.focusTarget = goal.focusTarget;
    }
}



void PlayerRushCameraSystem::SpringStep(float &x, float &v, float goal, float omega, float zeta, float dt) const
{
    // 半陰的オイラー。速さを先に更新し、新しい速さで位置を進める
    // omega*dt が大きいと発散するので、1刻みが kMaxStepArg を超えないように分ける
    const int   steps = std::clamp(static_cast<int>(std::ceil(omega * dt / kMaxStepArg)), 1, kMaxSubstep);
    const float h     = dt / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i)
    {
        const float accel = omega * omega * (goal - x) - 2.0f * zeta * omega * v;
        v += accel * h;
        x += v * h;
    }
}



float PlayerRushCameraSystem::DecideYawSign(const PlayerRushComponent &rush, EntityID cameraID) const
{
    const auto *tps = (cameraID != InvalidEntityID) ? m_world->GetComponent<PlayerCameraComponent>(cameraID) : nullptr;
    if (!tps) return 1.0f;

    // カメラが水平に見ている向きと、その右
    // 左手系なので、右は (z, 0, -x)
    const Vector3 viewDir  = CameraViewDir(tps->currentYaw, 0.0f);
    const Vector3 rightDir = {viewDir.z, 0.0f, -viewDir.x};

    // 右へ避けたら -1、左へ避けたら +1
    // 前後に避けたときも、右か左の成分で決める
    if (rush.evadeDir.Dot(rightDir) > 0.0f) return -1.0f;
    return 1.0f;
}



// ---------------------------------------------------------------------------
// 瞬間の演出
// ---------------------------------------------------------------------------

void PlayerRushCameraSystem::PunchStrike(const PlayerRushCameraComponent &cam, EntityID playerID, EntityID cameraID)
{
    // 画角パンチは「瞬間的にこの画角にする」要求なので、今の画角に足した値を渡す
    // その後は fovDamping で、攻撃の寄りの画角へ戻っていく
    if (cam.strikeFovPunch > 0.0f)
    {
        if (const auto *vcam = (cameraID != InvalidEntityID) ? m_world->GetComponent<VirtualCamera>(cameraID) : nullptr)
        {
            m_world->GetEventBus().Publish(CameraFovPunchRequestEvent{playerID, vcam->fov + cam.strikeFovPunch});
        }
    }

    if (cam.strikeShake > 0.0f)
    {
        m_world->GetEventBus().Publish(CameraShakeRequestEvent{cam.strikeShake});
    }
}



// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void PlayerRushCameraSystem::OnGui()
{
    m_world->ViewEach<PlayerRushCameraComponent>([&](EntityID id) {
        const auto *cam = m_world->GetComponent<PlayerRushCameraComponent>(id);
        if (!cam) return;

        ImGui::PushID(static_cast<int>(id));
        ImGui::Text("Entity #%u", EntityHandle::GetIndex(id));

        if (cam->weight > 0.0f)
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "weight : %.2f", cam->weight);
        }
        else
        {
            ImGui::TextDisabled("weight : 0 (slot released)");
        }
        ImGui::Text("yawSign : %+.0f", cam->yawSign);

        const CameraFramingParams &p = cam->current;
        ImGui::Text("distance %+.2f m   fov %+.1f   pitch %+.1f   yaw %+.1f", p.distanceOffset, p.fovOffset,
                    p.pitchOffset, p.yawOffset);
        ImGui::Text("roll %+.1f   followBoost %.1f", p.roll, p.followBoost);
        ImGui::Text("focus %.2f -> #%u", p.focus,
                    (p.focusTarget != InvalidEntityID) ? EntityHandle::GetIndex(p.focusTarget) : 0u);
        ImGui::TextDisabled("値の調整は Rush Camera Component（プレイヤー）で行う");

        ImGui::PopID();
    });
}



// カメラの target 解決（R00b）の後、カメラ本体（R01_Camera）の前に構図の要求を書く
REGISTER_RENDER_SYSTEM(PlayerRushCameraSystem, Priority::RenderStage::R00b_CameraTargetBind);
