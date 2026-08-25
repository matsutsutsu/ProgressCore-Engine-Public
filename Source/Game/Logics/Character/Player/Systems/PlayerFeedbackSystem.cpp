#include "PlayerFeedbackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"

#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

// Post Process
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
// Other Effects
#include "Engine/Graphics/Components/Impact/JustEvadeEffectComponent.h"

#include "Engine/Platform/Input/IInputAPI.h"

// System
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <imgui.h>
#include <algorithm>

namespace
{
// =========================================================================
// Utility
// =========================================================================

float TickTimer(float &timer, float dt)
{
    timer = std::max(0.0f, timer - dt);
    return timer;
}

// =========================================================================
// Envelope
// =========================================================================
//
// タイマーの残り時間を0～1へ正規化し、
// e^2のカーブを適用する。
//
// 被弾直後
//     env = 1
//
// 時間経過
//     env → 0
//
// 「パッと出て、スッと消える」演出にする。
// =========================================================================

float CalculateEnvelope(float timer, float duration)
{
    if (duration <= 0.0f)
        return 0.0f;

    const float normalizedTime =
        std::clamp(timer / duration, 0.0f, 1.0f);

    return normalizedTime * normalizedTime;
}

// =========================================================================
// Lerp
// =========================================================================

float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
} // namespace

PlayerFeedbackSystem::PlayerFeedbackSystem()
    : IfSystem("PlayerFeedbackSystem")
{
    hasGui = true;
}

void PlayerFeedbackSystem::Initialize()
{
    // =========================================================================
    // プレイヤー被弾
    // =========================================================================

    ListenEvent<DamageTakenEvent>(
        [this](const DamageTakenEvent &event) {
            if (!event.isPlayer)
                return;

            OnPlayerDamaged(event);
        });

    // =========================================================================
    // プレイヤー攻撃命中
    // =========================================================================

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

void PlayerFeedbackSystem::OnGui()
{
    // =========================================================================
    // Camera Shake
    // =========================================================================

    if (ImGui::TreeNode("Damage Camera Shake"))
    {
        ImGui::DragFloat(
            "Damage Multiplier",
            &m_shakeDamageMultiplier,
            0.01f,
            0.0f,
            2.0f,
            "%.2f");

        ImGui::DragFloat(
            "Min Strength",
            &m_shakeMinStrength,
            0.01f,
            0.0f,
            10.0f,
            "%.2f");

        ImGui::DragFloat(
            "Max Strength",
            &m_shakeMaxStrength,
            0.01f,
            0.0f,
            20.0f,
            "%.2f");

        ImGui::DragFloat(
            "Duration",
            &m_shakeDuration,
            0.01f,
            0.0f,
            5.0f,
            "%.2f sec");

        ImGui::TreePop();
    }

    // =========================================================================
    // Controller Vibration
    // =========================================================================

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

    // =========================================================================
    // Attack Hit Controller Vibration
    // =========================================================================

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



    // =========================================================================
    // Damage Vignette
    // =========================================================================

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

    // =========================================================================
    // Damage Chromatic Aberration
    // =========================================================================

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

// ===================================================================================
// Damage Feedback
// ===================================================================================

void PlayerFeedbackSystem::OnPlayerDamaged(
    const DamageTakenEvent &event)
{
    StartDamageCameraShake(event.damageAmount);

    StartDamageVibration();

    StartDamageVignette();

    StartDamageChromaticAberration();

    // TODO:
    // StartDamageSound();
}

// ===================================================================================
// Player Attack Hit
// ===================================================================================

void PlayerFeedbackSystem::OnPlayerAttackHit(
    const AttackHitConfirmedEvent &event)
{
    // 攻撃者がプレイヤーでなければ無視。
    if (!m_world->HasComponent<PlayerComponent>(event.attackerID)) return;

    StartAttackHitVibration(event);

    // TODO:
    // StartAttackHitCameraShake(event);
    // StartAttackHitSound(event);
}


// ===================================================================================
// Camera Shake
// ===================================================================================

void PlayerFeedbackSystem::StartDamageCameraShake(
    float damageAmount)
{
    const float shakeStrength =
        std::clamp(
            damageAmount * m_shakeDamageMultiplier,
            m_shakeMinStrength,
            m_shakeMaxStrength);

    auto vcamView = m_world->View<VirtualCamera>();

    for (auto camID : vcamView)
    {
        if (auto *shake =
                m_world->GetComponent<CameraShake>(camID))
        {
            shake->SetShake(
                shakeStrength,
                m_shakeDuration);
        }
        else
        {
            CameraShake newShake;

            newShake.SetShake(
                shakeStrength,
                m_shakeDuration);

            m_world->AddComponent<CameraShake>(
                camID,
                newShake);
        }
    }
}

// ===================================================================================
// Controller Vibration
// ===================================================================================

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

    // ---------------------------------------------------------
    // 命中の強さに応じて振動をスケーリング
    // ---------------------------------------------------------

  
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


// ===================================================================================
// Damage Vignette
// ===================================================================================

void PlayerFeedbackSystem::StartDamageVignette()
{
    m_vignetteTimer = m_vignetteDuration;
}

// ===================================================================================
// Update Damage Vignette
// ===================================================================================

void PlayerFeedbackSystem::UpdateDamageVignette(float dt)
{
    auto *vignette = m_world->GetSingletonComponent<VignetteConfigComponent>();

    if (!vignette) return;

       
    // -------------------------------------------------------------------------
    // Just Evadeとの競合を確認
    // -------------------------------------------------------------------------

    if (auto *justEvade =
            m_world->GetSingletonComponent<JustEvadeEffectComponent>())
    {
        if (justEvade->timer > 0.0f ||justEvade->hold)       
        {
            // JustEvade側が正しいBaselineを取得できるよう、
            // 被弾ビネットの値を先に元へ戻す。
            if (m_vignetteActive)
            {
                vignette->enable = m_vignetteBase.enable;                 
                vignette->data.vigIntensity = m_vignetteBase.intensity;
                vignette->data.vigColor = m_vignetteBase.color;
                vignette->data.vigPulseSpeed = m_vignetteBase.pulse;
                vignette->data.vigRadius = m_vignetteBase.radius;
                vignette->data.vigSoftness = m_vignetteBase.softness;

                m_vignetteActive = false;
            }

            m_vignetteTimer = 0.0f;

            return;
        }
    }

    // -------------------------------------------------------------------------
    // 演出終了
    // -------------------------------------------------------------------------

    if (m_vignetteTimer <= 0.0f)
    {
        if (m_vignetteActive)
        {
            vignette->enable =
                m_vignetteBase.enable;

            vignette->data.vigIntensity =
                m_vignetteBase.intensity;

            vignette->data.vigColor =
                m_vignetteBase.color;

            vignette->data.vigPulseSpeed =
                m_vignetteBase.pulse;

            vignette->data.vigRadius =
                m_vignetteBase.radius;

            vignette->data.vigSoftness =
                m_vignetteBase.softness;

            m_vignetteActive = false;
        }

        return;
    }

    // -------------------------------------------------------------------------
    // 演出開始時にBaselineを保存
    // -------------------------------------------------------------------------

    if (!m_vignetteActive)
    {
        m_vignetteBase.enable =
            vignette->enable;

        m_vignetteBase.intensity =
            vignette->data.vigIntensity;

        m_vignetteBase.color =
            vignette->data.vigColor;

        m_vignetteBase.pulse =
            vignette->data.vigPulseSpeed;

        m_vignetteBase.radius =
            vignette->data.vigRadius;

        m_vignetteBase.softness =
            vignette->data.vigSoftness;

        m_vignetteActive = true;
    }

    // -------------------------------------------------------------------------
    // タイマー更新
    // -------------------------------------------------------------------------

    TickTimer(
        m_vignetteTimer,
        dt);

    // -------------------------------------------------------------------------
    // エンベロープ
    // -------------------------------------------------------------------------

    const float env =
        CalculateEnvelope(
            m_vignetteTimer,
            m_vignetteDuration);

    // -------------------------------------------------------------------------
    // Vignetteへ反映
    // -------------------------------------------------------------------------

    vignette->enable = true;

    vignette->data.vigIntensity =
        Lerp(
            m_vignetteBase.intensity,
            m_vignettePeak,
            env);

    vignette->data.vigColor =
        DirectX::XMFLOAT4{
            Lerp(
                m_vignetteBase.color.x,
                m_vignetteR,
                env),

            Lerp(
                m_vignetteBase.color.y,
                m_vignetteG,
                env),

            Lerp(
                m_vignetteBase.color.z,
                m_vignetteB,
                env),

            1.0f};

    vignette->data.vigRadius =
        Lerp(
            m_vignetteBase.radius,
            m_vignetteRadius,
            env);

    vignette->data.vigSoftness =
        Lerp(
            m_vignetteBase.softness,
            m_vignetteSoftness,
            env);

    vignette->data.vigPulseSpeed = 0.0f;
}

// ===================================================================================
// Damage Chromatic Aberration
// ===================================================================================

void PlayerFeedbackSystem::StartDamageChromaticAberration()
{
    m_chromaticTimer = m_chromaticDuration;
}

// ===================================================================================
// Update Damage Chromatic Aberration
// ===================================================================================

void PlayerFeedbackSystem::UpdateDamageChromaticAberration(float dt)
{
    auto *chromatic =
        m_world->GetSingletonComponent<
            ChromaticAberrationConfigComponent>();

    if (!chromatic)
        return;

    // -------------------------------------------------------------------------
    // 演出終了
    // -------------------------------------------------------------------------

    if (m_chromaticTimer <= 0.0f)
    {
        if (m_chromaticActive)
        {
            chromatic->enable =
                m_chromaticBase.enable;

            chromatic->data.chromaCenter =
                m_chromaticBase.center;

            chromatic->data.chromaStrength =
                m_chromaticBase.strength;

            chromatic->data.chromaFalloff =
                m_chromaticBase.falloff;

            m_chromaticActive = false;
        }

        return;
    }

    // -------------------------------------------------------------------------
    // 演出開始時にBaselineを保存
    // -------------------------------------------------------------------------

    if (!m_chromaticActive)
    {
        m_chromaticBase.enable =
            chromatic->enable;

        m_chromaticBase.center =
            chromatic->data.chromaCenter;

        m_chromaticBase.strength =
            chromatic->data.chromaStrength;

        m_chromaticBase.falloff =
            chromatic->data.chromaFalloff;

        m_chromaticActive = true;
    }

    // -------------------------------------------------------------------------
    // タイマー更新
    // -------------------------------------------------------------------------

    TickTimer(m_chromaticTimer, dt);

    // -------------------------------------------------------------------------
    // エンベロープ
    // -------------------------------------------------------------------------

    const float env =
        CalculateEnvelope(
            m_chromaticTimer,
            m_chromaticDuration);

    // -------------------------------------------------------------------------
    // Chromatic Aberrationへ反映
    // -------------------------------------------------------------------------

    chromatic->enable = true;

    chromatic->data.chromaCenter = m_chromaticBase.center;

    chromatic->data.chromaStrength = Lerp(
        m_chromaticBase.strength,
        m_chromaticPeak,
        env);

    chromatic->data.chromaFalloff = m_chromaticBase.falloff;
}

// System Registration
REGISTER_LOGIC_SYSTEM(PlayerFeedbackSystem, Priority::LogicStage::L06_Resolution);
