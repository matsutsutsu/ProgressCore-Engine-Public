#include "PlayerFeedbackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"

#include "Engine/Camera/CameraEvents.h" // CameraShakeRequestEvent（画面揺れの唯一の入口）
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

// Post Process
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
// Other Effects

#include "Engine/Platform/Input/IInputAPI.h"

// System
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// Shipping(Release)ではImGui本体もインクルードパスも構成から外れるため、
// エディタ有効時のみ取り込む。
#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif
#include "Engine/Core/Math/StringHash.h"

#include <algorithm>

namespace
{
// =========================================================================
// PostProcess スロット
// =========================================================================
// スロットの識別子と重ね順。エンジンは中身を知らないので、名前と優先度は使う側が決める。
// 命中バースト(10)・ジャスト回避(20)より弱い。被弾は他の演出に譲る。

const uint32_t kSource   = CCL::Utils::HashString("PlayerDamage");
constexpr int  kPriority = 5;

// ダッシュ集中線は常時出る環境演出なので最弱。命中バーストやジャスト回避に前を譲る。
// （放射ブラーの合成は最大値なので実際は順序に依存しないが、意図として明示しておく）
const uint32_t kDashSource   = CCL::Utils::HashString("PlayerDash");
constexpr int  kDashPriority = 1;

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

    UpdateDashRadialBlur();
}

// ===================================================================================
// Dash Radial Blur
// ===================================================================================

void PlayerFeedbackSystem::UpdateDashRadialBlur()
{
    auto *blur = m_world->GetSingletonComponent<RadialBlurConfigComponent>();
    if (!blur) return;

    // ダッシュ状態そのものから毎フレーム引く。被弾演出のような専用タイマーは持たない。
    // currentTimer はダッシュ終了後クールダウンに再利用されるため、必ず isDashing で門番する。
    float env  = 0.0f;
    float peak = 0.0f;

    ForEach([&](const DashComponent &dash) {
        if (dash.isDashing)
        {
            const float e = CalculateEnvelope(dash.currentTimer, dash.dashDuration);
            if (e > env) { env = e; peak = m_dashBlurPeak; }
        }
        else if (dash.isApproaching)
        {
            const float e = CalculateEnvelope(dash.approachTimer, dash.approachDuration);
            if (e > env) { env = e; peak = m_approachBlurPeak; }
        }
    });

    // 誰もダッシュしていない → スロットを手放す。ForEach が空振りしても必ずここを通るので
    // プレイヤーが消えた場合でも集中線が出っぱなしにならない。
    if (env <= 0.0f || peak <= 0.0f)
    {
        blur->overrides.Release(kDashSource);
        return;
    }

    if (auto *ov = blur->overrides.Acquire(kDashSource, kDashPriority))
    {
        ov->weight          = env;
        ov->params.strength = peak;
    }
}

void PlayerFeedbackSystem::OnGui()
{
#ifdef ENABLE_EDITOR
    // =========================================================================
    // Camera Shake
    // =========================================================================

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

    // =========================================================================
    // Dash Radial Blur
    // =========================================================================

    if (ImGui::TreeNode("Dash Radial Blur"))
    {
        ImGui::SliderFloat(
            "Dash Peak",
            &m_dashBlurPeak,
            0.0f,
            1.0f,
            "%.3f");

        ImGui::SliderFloat(
            "Approach Peak",
            &m_approachBlurPeak,
            0.0f,
            1.0f,
            "%.3f");

        ImGui::TextDisabled("継続時間は DashComponent 側の設定に従う");

        ImGui::TreePop();
    }
#endif
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
    // 以前はここから全ての VirtualCamera へ直接 SetShake していたため、
    // 着地の揺れを被弾の揺れが上書きして消していた。要求を出すだけにすると、
    // 受け側(CameraShakeSystem)が蓄積して1つの揺れにまとめてくれる。
    const float trauma =
        std::clamp(
            damageAmount * m_traumaPerDamage,
            m_traumaMin,
            m_traumaMax);

    m_world->GetEventBus().Publish(CameraShakeRequestEvent{trauma});
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

    // 演出終了。自分のスロットを手放すだけでよい（素の設定も他演出も触らない）。
    if (m_vignetteTimer <= 0.0f)
    {
        vignette->overrides.Release(kSource);
        return;
    }

    TickTimer(m_vignetteTimer, dt);

    const float env = CalculateEnvelope(m_vignetteTimer, m_vignetteDuration);

    // 「どこへ寄せたいか(目標値)」と「どれだけ寄せるか(weight)」だけを書く。
    // 素の設定からの補間と、他演出との重ね合わせは Resolve() が担当する。
    if (auto *ov = vignette->overrides.Acquire(kSource, kPriority))
    {
        ov->weight            = env;
        ov->params.intensity  = m_vignettePeak;
        ov->params.color      = DirectX::XMFLOAT4{m_vignetteR, m_vignetteG, m_vignetteB, 1.0f};
        ov->params.radius     = m_vignetteRadius;
        ov->params.softness   = m_vignetteSoftness;
        ov->params.pulseSpeed = 0.0f;
    }
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
    //
    // 触るのは自分のスロットだけ。設定本体(enable/data)や他演出のスロットには
    // 一切書かないので、命中バーストやジャスト回避演出の最中に被弾しても
    // 互いを消し合わない。最終強度の合成は PostProcessSystem が行う。

    if (m_chromaticTimer <= 0.0f)
    {
        chromatic->overrides.Release(kSource);
        return;
    }

    // -------------------------------------------------------------------------
    // タイマー更新
    // -------------------------------------------------------------------------

    TickTimer(m_chromaticTimer, dt);

    // -------------------------------------------------------------------------
    // エンベロープ → 上乗せ量
    // -------------------------------------------------------------------------

    const float env =
        CalculateEnvelope(
            m_chromaticTimer,
            m_chromaticDuration);

    if (auto *ov = chromatic->overrides.Acquire(kSource, kPriority))
    {
        ov->weight          = env;
        ov->params.strength = m_chromaticPeak;
    }
}

// System Registration
REGISTER_LOGIC_SYSTEM(PlayerFeedbackSystem, Priority::LogicStage::L06_Resolution);
