#include "PlayerFeedbackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"

#include "Engine/Camera/CameraEvents.h" // CameraShakeRequestEvent（画面揺れの要求はこのイベントだけで出す）
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/PostFx.h"

#include "Engine/Platform/Input/IInputAPI.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <imgui.h>
#include "Engine/Core/Math/StringHash.h"

#include <algorithm>

namespace
{
    // PostProcess の上書きスロットの識別子と優先度。名前と優先度は使う側が決める。
    // 被弾は、命中の演出(ScreenImpactSystem=10)・ジャスト回避(JustEvadeEffectSystem=20)より優先度を下げる。

    const uint32_t kSource   = CCL::Utils::HashString("PlayerDamage");
    constexpr int  kPriority = 5;

    float TickTimer(float &timer, float dt)
    {
        timer = std::max(0.0f, timer - dt);
        return timer;
    }

    // 残り時間を 0〜1 に正規化して2乗した値（演出の強さの係数）を返す。
    // 開始直後が 1 で、前半に大きく下がり、後半はゆっくり 0 に近づく。

    float CalculateEnvelope(float timer, float duration)
    {
        if (duration <= 0.0f)
            return 0.0f;

        const float normalizedTime =
            std::clamp(timer / duration, 0.0f, 1.0f);

        return normalizedTime * normalizedTime;
    }
} // namespace

PlayerFeedbackSystem::PlayerFeedbackSystem()
    : IfSystem("PlayerFeedbackSystem")
{
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void PlayerFeedbackSystem::Initialize()
{
    ListenEvent<DamageTakenEvent>(
        [this](const DamageTakenEvent &event) {
            if (!event.isPlayer)
                return;

            OnPlayerDamaged(event);
        });

    ListenEvent<AttackHitConfirmedEvent>(
        [this](const AttackHitConfirmedEvent &event) {
            OnPlayerAttackHit(event);
        });
}

void PlayerFeedbackSystem::Update(float dt)
{
    UpdateDamageVignette(dt);

    UpdateDamageChromaticAberration(dt);
}

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void PlayerFeedbackSystem::OnGui()
{
    if (ImGui::TreeNode("Damage Camera Shake"))
    {
        ImGui::TextWrapped(
            "ここで決めるのは被弾の「衝撃量」だけ。振幅・周波数・揺れる長さは"
            "カメラの Camera Shake コンポーネント側で調整する。");

        ImGui::DragFloat(
            "Trauma / Damage",
            &m_traumaPerDamage,
            0.005f,
            0.0f,
            1.0f,
            "%.3f");

        ImGui::DragFloat(
            "Min Trauma",
            &m_traumaMin,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::DragFloat(
            "Max Trauma",
            &m_traumaMax,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Damage Controller Vibration"))
    {
        ImGui::SliderFloat(
            "Left Motor",
            &m_vibrationLeft,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::SliderFloat(
            "Right Motor",
            &m_vibrationRight,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::DragFloat(
            "Duration##Vibration",
            &m_vibrationDuration,
            0.01f,
            0.0f,
            5.0f,
            "%.2f sec");

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Attack Hit Controller Vibration"))
    {
        ImGui::SliderFloat(
            "Left Motor##AttackHit",
            &m_attackHitVibrationLeft,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::SliderFloat(
            "Right Motor##AttackHit",
            &m_attackHitVibrationRight,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::DragFloat(
            "Duration##AttackHitVibration",
            &m_attackHitVibrationDuration,
            0.005f,
            0.0f,
            1.0f,
            "%.3f sec");

        ImGui::SliderFloat(
            "Feedback Scale",
            &m_attackHitFeedbackScale,
            0.0f,
            2.0f,
            "%.2f");

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Damage Vignette"))
    {
        ImGui::DragFloat(
            "Duration##Vignette",
            &m_vignetteDuration,
            0.01f,
            0.01f,
            5.0f,
            "%.2f sec");

        ImGui::SliderFloat(
            "Peak Intensity",
            &m_vignettePeak,
            0.0f,
            2.0f,
            "%.2f");

        ImGui::ColorEdit3(
            "Flash Color",
            &m_vignetteR);

        ImGui::SliderFloat(
            "Radius",
            &m_vignetteRadius,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::SliderFloat(
            "Softness",
            &m_vignetteSoftness,
            0.0f,
            1.0f,
            "%.2f");

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Damage Chromatic Aberration"))
    {
        ImGui::DragFloat(
            "Duration##Chromatic",
            &m_chromaticDuration,
            0.01f,
            0.01f,
            5.0f,
            "%.2f sec");

        ImGui::SliderFloat(
            "Peak Strength",
            &m_chromaticPeak,
            0.0f,
            1.0f,
            "%.3f");

        ImGui::TreePop();
    }
}

// ---------------------------------------------------------------------------
// イベントの受け口
// ---------------------------------------------------------------------------

void PlayerFeedbackSystem::OnPlayerDamaged(
    const DamageTakenEvent &event)
{
    StartDamageCameraShake(event.damageAmount);

    StartDamageVibration();

    StartDamageVignette();

    StartDamageChromaticAberration();

    // TODO(2026-08-25): 被弾の効果音を鳴らす
}

void PlayerFeedbackSystem::OnPlayerAttackHit(
    const AttackHitConfirmedEvent &event)
{
    if (!m_world->HasComponent<PlayerComponent>(event.attackerID)) return;

    StartAttackHitVibration(event);

    // TODO(2026-08-25): 命中時のカメラの揺れと効果音を足す
}

// ---------------------------------------------------------------------------
// 揺れと振動
// ---------------------------------------------------------------------------

void PlayerFeedbackSystem::StartDamageCameraShake(
    float damageAmount)
{
    // VirtualCamera へ直接書かない。直接書くと値の上書きになり、着地の揺れを被弾の揺れが消してしまう。
    // 要求のイベントを出せば、受け側(CameraShakeSystem)が足し合わせて1つの揺れにする。
    const float trauma =
        std::clamp(
            damageAmount * m_traumaPerDamage,
            m_traumaMin,
            m_traumaMax);

    m_world->GetEventBus().Publish(CameraShakeRequestEvent{trauma});
}

void PlayerFeedbackSystem::StartDamageVibration()
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>())
        return;

    auto inputAPI =
        m_world->GetResource<std::shared_ptr<IInputAPI>>();

    if (!inputAPI)
        return;

    inputAPI->SetVibration(
        m_vibrationLeft,
        m_vibrationRight,
        m_vibrationDuration);
}

void PlayerFeedbackSystem::StartAttackHitVibration(
    const AttackHitConfirmedEvent &event)
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;

    auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();

    if (!inputAPI) return;

    // 命中のダメージ量に応じて振動を強める（0.25〜1.0 倍）

    const float power =
        std::clamp(
            event.damage / m_attackHitFeedbackScale,
            0.25f,
            1.0f);

    const float left = m_attackHitVibrationLeft * power;

    const float right = m_attackHitVibrationRight * power;

    inputAPI->SetVibration(
        left,
        right,
        m_attackHitVibrationDuration);
}

// ---------------------------------------------------------------------------
// 画面効果（ビネット・色収差）
// ---------------------------------------------------------------------------

void PlayerFeedbackSystem::StartDamageVignette()
{
    m_vignetteTimer = m_vignetteDuration;
}

void PlayerFeedbackSystem::UpdateDamageVignette(float dt)
{
    // 演出が終わったら、自分のスロットを手放すだけでよい（元の設定にも他の演出にも触らない）。
    //
    // 色収差とは別のタイマーで終わるので、ReleaseAll ではなくビネットだけを手放す。
    if (m_vignetteTimer <= 0.0f)
    {
        CCL::PostFx::Release<VignetteConfigComponent>(*m_world, kSource);
        return;
    }

    TickTimer(m_vignetteTimer, dt);

    const float env = CalculateEnvelope(m_vignetteTimer, m_vignetteDuration);

    // 目標値と、そこへ寄せる割合(weight)だけを書く。
    // 元の設定からの補間と、他の演出との重ね合わせは Resolve() が行う。
    if (auto *ov = CCL::PostFx::Vignette(*m_world, kSource, kPriority))
    {
        ov->weight            = env;
        ov->params.intensity  = m_vignettePeak;
        ov->params.color      = DirectX::XMFLOAT4{m_vignetteR, m_vignetteG, m_vignetteB, 1.0f};
        ov->params.radius     = m_vignetteRadius;
        ov->params.softness   = m_vignetteSoftness;
        ov->params.pulseSpeed = 0.0f;
    }
}

void PlayerFeedbackSystem::StartDamageChromaticAberration()
{
    m_chromaticTimer = m_chromaticDuration;
}

void PlayerFeedbackSystem::UpdateDamageChromaticAberration(float dt)
{
    // 触るのは自分のスロットだけ。設定本体(enable/data)や他の演出のスロットには書かないので、
    // 命中やジャスト回避の演出の最中に被弾しても、互いを消し合わない。最終的な強さは PostProcessSystem が合成する。
    //
    // ビネットとは別のタイマーで終わるので、ReleaseAll ではなく色収差だけを手放す。
    if (m_chromaticTimer <= 0.0f)
    {
        CCL::PostFx::Release<ChromaticAberrationConfigComponent>(*m_world, kSource);
        return;
    }

    TickTimer(m_chromaticTimer, dt);

    const float env =
        CalculateEnvelope(
            m_chromaticTimer,
            m_chromaticDuration);

    if (auto *ov = CCL::PostFx::ChromaticAberration(*m_world, kSource, kPriority))
    {
        ov->weight          = env;
        ov->params.strength = m_chromaticPeak;
    }
}

std::vector<CCL::ECS::TypeID> PlayerFeedbackSystem::GetWriteTypes() const
{
    return {CCL::ECS::TypeInfo<VignetteConfigComponent>::ID(),
            CCL::ECS::TypeInfo<ChromaticAberrationConfigComponent>::ID()};
}

REGISTER_LOGIC_SYSTEM(PlayerFeedbackSystem, Priority::LogicStage::L06_Resolution);
