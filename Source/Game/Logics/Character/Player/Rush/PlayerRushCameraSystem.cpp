#include "PlayerRushCameraSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h"                       // Damp / SmoothStep
#include "Engine/Core/Math/StringHash.h"                          // 上書きスロットの発生源キー
#include "Engine/Camera/CameraEvents.h"                           // 揺れ・画角パンチの要求
#include "Engine/Camera/Components/VirtualCameraComponents.h"     // 画角パンチの基準にする今の画角
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // 構図の上書きスロット
#include "Engine/Time/TimeService.h"                              // PresentationDt（ポーズ中は止める）
#include "Game/Logics/Combat/Components/DeadTag.h"                // 注視先が死んでいないか判定

#include <imgui.h> // デバッグ UI(OnGui)

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    const uint32_t kSource   = CCL::Utils::HashString("RushCamera"); // 構図の上書きスロットの発生源
    constexpr int  kPriority = 10;                                    // 他の演出と重なったときの順（大きいほど後）

    // ばねの角振動数 = kSettle / blendTime。減衰比 1（行き過ぎ無し）のとき、blendTime 秒で目標の約95%に届く。
    constexpr float kSettle = 4.7f;

    constexpr float kMinZeta    = 0.2f;  // 減衰比の下限。これより小さいと何度も揺れ返して酔う
    constexpr float kMaxStepArg = 0.25f; // 1刻みの omega*dt の上限。超えると半陰的オイラーが発散するので刻む
    constexpr int   kMaxSubstep = 8;     // 刻みの上限（ヒッチで dt が大きいフレームでも回しすぎない）
} // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

std::vector<TypeID> PlayerRushCameraSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<CameraBodyTPSComponent>::ID());
    return types;
}

std::vector<TypeID> PlayerRushCameraSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(TypeInfo<VirtualCamera>::ID());
    return types;
}

void PlayerRushCameraSystem::Update(float dt)
{
    // 処理フロー:
    // 1. ラッシュの開始と斬りの瞬間を検出し、揺れと画角パンチを要求する
    // 2. 今のフェーズの構図へ寄せる（終了後は効きを落とす）
    // 3. カメラの構図の上書きスロットへ書く（効きが 0 になったら手放す）

    // 描画ステージは実時間で回る。ポーズ中だけ dt=0 にして構図を止める。
    dt = Time::PresentationDt(*m_world, dt);

    ForEachWithID([&](EntityID                   id,
                      PlayerRushCameraComponent       &cam,
                      const PlayerRushComponent       &rush,
                      const PlayerComponent     &player) {
        const EntityID cameraID = FindCamera(id);

        // ---------------------------------------------------------
        // 1. 開始・斬りの瞬間
        // ---------------------------------------------------------
        if (rush.isActive && !cam.wasActive)
        {
            // 前の演出が戻りきっていれば、ばねを静止状態から始める。
            // 戻り途中で次が始まったときは、今の構図と速さのまま続けて跳ばないようにする。
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

        // 斬りは Attack 中に attackFired が立った瞬間。前のラッシュの立ちっぱなしを拾わないよう、
        // Attack フェーズの間だけを見る。
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
            const CameraFramingParams goal = ToFraming(shot, cam.yawSign, PickFocusTarget(player));
            SpringToward(cam.current, cam.velocity, goal, shot.blendTime, shot.bounce, dt);
        }
        else if (cam.weight > 0.0f)
        {
            // 終了後は構図を保ったまま効きだけを落とす（元の構図へ線形に戻し、SmoothStep で端を丸める）。
            const float releaseTime = (std::max)(cam.releaseTime, 0.01f);
            cam.weight              = (std::max)(0.0f, cam.weight - dt / releaseTime);
        }

        // ---------------------------------------------------------
        // 3. カメラへ書く
        // ---------------------------------------------------------
        auto *tps = (cameraID != InvalidEntityID) ? m_world->GetComponent<CameraBodyTPSComponent>(cameraID) : nullptr;
        if (!tps)
        {
            return;
        }

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
    // ヨーと傾きは開始時に決めた左右を掛ける（避けた側の反対から、同じ側へ傾けて映す）。
    // 注視先が居なければ寄せない（割合だけ残すと、寄せ先の無い寄せになる）。
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
    // 指数のイーズ（Damp）は必ず手前で止まるので、「勢いよく引いて止まる」反動が出せない。
    // ばねなら減衰比を下げるだけで行き過ぎて戻る動きになり、bounce=0 なら従来どおり滑らかに届く。
    const float omega = kSettle / (std::max)(blendTime, 0.01f);
    const float zeta  = std::clamp(1.0f - bounce, kMinZeta, 1.0f);

    SpringStep(current.distanceOffset, velocity.distanceOffset, goal.distanceOffset, omega, zeta, dt);
    SpringStep(current.fovOffset, velocity.fovOffset, goal.fovOffset, omega, zeta, dt);
    SpringStep(current.pitchOffset, velocity.pitchOffset, goal.pitchOffset, omega, zeta, dt);
    SpringStep(current.yawOffset, velocity.yawOffset, goal.yawOffset, omega, zeta, dt);
    SpringStep(current.roll, velocity.roll, goal.roll, omega, zeta, dt);
    SpringStep(current.followBoost, velocity.followBoost, goal.followBoost, omega, zeta, dt);
    SpringStep(current.focus, velocity.focus, goal.focus, omega, zeta, dt);

    // 行き過ぎで範囲の外へ出ないようにする（寄せる割合と追従の足し込みは負にならない）。
    current.focus       = std::clamp(current.focus, 0.0f, 1.0f);
    current.followBoost = (std::max)(current.followBoost, 0.0f);

    // 寄せ先は切り替えるだけ。寄せ先が居なくなったときは前の先を残し、割合だけを 0 へ落とす
    // （先を先に消すと、注視点がその場で跳ぶ）。
    if (goal.focusTarget != InvalidEntityID)
    {
        current.focusTarget = goal.focusTarget;
    }
}

void PlayerRushCameraSystem::SpringStep(float &x, float &v, float goal, float omega, float zeta, float dt) const
{
    // 半陰的オイラー（速さを先に更新し、新しい速さで位置を進める）。
    // omega*dt が大きいと発散するので、1刻みが kMaxStepArg を超えないように分ける。
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
    const auto *tps = (cameraID != InvalidEntityID) ? m_world->GetComponent<CameraBodyTPSComponent>(cameraID) : nullptr;
    if (!tps)
    {
        return 1.0f;
    }

    // カメラの前方（水平）。CameraActionTPSSystem は pivot からカメラへの向きを (cos yaw, sin yaw) で置くので、
    // 前方はその逆向き。
    const float yawRad = DirectX::XMConvertToRadians(tps->currentYaw);
    const float fwdX   = -std::cos(yawRad);
    const float fwdZ   = -std::sin(yawRad);

    // 前方 × 回避方向 の y 成分で、回避がカメラから見て左右どちらかを判定する。
    const float crossY = fwdZ * rush.evadeDir.x - fwdX * rush.evadeDir.z;
    return (crossY >= 0.0f) ? 1.0f : -1.0f;
}

// ---------------------------------------------------------------------------
// 瞬間の演出
// ---------------------------------------------------------------------------

void PlayerRushCameraSystem::PunchStrike(const PlayerRushCameraComponent &cam, EntityID playerID, EntityID cameraID)
{
    // 画角パンチは「瞬間的にこの画角にする」要求なので、今の画角に足した値を渡す。
    // 以降は fovDamping で目標の画角（攻撃の寄り）へ戻っていく。
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
// 探索
// ---------------------------------------------------------------------------

EntityID PlayerRushCameraSystem::FindCamera(EntityID playerID) const
{
    EntityID found = InvalidEntityID;
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
        if (found != InvalidEntityID)
        {
            return;
        }
        const auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (tps && tps->target.id == playerID)
        {
            found = camID;
        }
    });
    return found;
}

EntityID PlayerRushCameraSystem::PickFocusTarget(const PlayerComponent &player) const
{
    const EntityID id = player.lockOnTargetID;
    if (id == InvalidEntityID || !m_world->IsEntityValid(id) || m_world->HasComponent<Tag::DeadTag>(id))
    {
        return InvalidEntityID;
    }
    return id;
}

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void PlayerRushCameraSystem::OnGui()
{
    m_world->ViewEach<PlayerRushCameraComponent>([&](EntityID id) {
        const auto *cam = m_world->GetComponent<PlayerRushCameraComponent>(id);
        if (!cam)
        {
            return;
        }

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

// カメラの target 解決（R00b）の後、カメラ本体（R01_Camera）の前に構図の要求を書く。
REGISTER_RENDER_SYSTEM(PlayerRushCameraSystem, Priority::RenderStage::R00b_CameraTargetBind);
