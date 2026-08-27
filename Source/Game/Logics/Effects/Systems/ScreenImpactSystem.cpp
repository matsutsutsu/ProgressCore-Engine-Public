// ===========================================================================
// ScreenImpactSystem.cpp
// 役割・設計方針は ScreenImpactSystem.h を参照。
//
// 実装の意図(Why):
//   - 強さ strength = (timer/duration) * power。残り時間で線形に減衰し、命中直後が最大。
//   - カメラ揺れは MainCamera の right/up 基準（画面空間）に減衰正弦のオフセットを重ねる。
//     eye と focus を同量ずらす並進シェイクなので視線方向は崩れない（酔いにくい）。
//   - dt はフレーム実時間。ヒットストップ中(ゲーム時間停止)でも揺れは進むため、
//     「時が止まって画面だけ震える」定番の手触りになる。
// ===========================================================================
#include "ScreenImpactSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Core/Math/StringHash.h"

#include <DirectXMath.h>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
/// スロットの識別子と重ね順。エンジンは中身を知らないので、名前と優先度は使う側が決める。
/// ジャスト回避(20)より弱くしておき、同時に出たら回避側が前に出るようにする。
const uint32_t kSource   = CCL::Utils::HashString("ScreenImpact");
constexpr int  kPriority = 10;
} // namespace

void ScreenImpactSystem::Update(float dt)
{
    auto* impact = m_world->GetSingletonComponent<ScreenImpactComponent>();

    const bool active = (impact && impact->timer > 0.0f);

    // ── PP への要求（自分のスロットだけを書く。設定本体には触れない） ──
    //   非アクティブ時は weight=0 ではなくスロットごと手放し、他の演出に枠を空ける。
    auto writeBurst = [&](float radialStrength, float chromaStrength) {
        if (auto* c = m_world->GetSingletonComponent<RadialBlurConfigComponent>())
        {
            if (auto* ov = c->overrides.Acquire(kSource, kPriority))
            {
                ov->weight          = 1.0f;
                ov->params.strength = radialStrength;
            }
        }
        if (auto* c = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>())
        {
            if (auto* ov = c->overrides.Acquire(kSource, kPriority))
            {
                ov->weight          = 1.0f;
                ov->params.strength = chromaStrength;
            }
        }
    };

    auto releaseBurst = [&]() {
        if (auto* c = m_world->GetSingletonComponent<RadialBlurConfigComponent>())
            c->overrides.Release(kSource);
        if (auto* c = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>())
            c->overrides.Release(kSource);
    };

    if (!active)
    {
        // 立ち下がり: 自分のスロットを手放す（設定本体・他演出には触れない）。
        if (m_wasActive)
        {
            releaseBurst();
            m_wasActive = false;
        }
        return;
    }

    // ── 1. 減衰 ──
    impact->timer -= dt;
    if (impact->timer < 0.0f) impact->timer = 0.0f;

    const float t        = (impact->duration > 0.0f) ? (impact->timer / impact->duration) : 0.0f;
    const float strength = t * impact->power; // 命中直後=最大 → 0 へ

    // ── 2. PPバースト（放射ブラー＋色収差） ──
    writeBurst(impact->radialStrength * strength, impact->chromaticStrength * strength);

    // ── 3. カメラシェイク（CameraBrain 確定後の MainCamera に重ねる） ──
    if (m_world->HasResource<Camera*>())
    {
        if (Camera* cam = m_world->GetResource<Camera*>())
        {
            const float amp   = impact->shakeAmplitude * strength;
            const float phase = (impact->duration - impact->timer) * impact->shakeFrequency;

            // 種で位相をずらし、XとYで異なる周波数比にして単調な往復を避ける。
            const float ox = std::sin(phase          + impact->seed);
            const float oy = std::sin(phase * 1.37f  + impact->seed * 2.1f);

            const XMFLOAT3 eye   = cam->GetEye();
            const XMFLOAT3 focus = cam->GetFocus();
            const XMFLOAT3 up    = cam->GetUp();
            const XMFLOAT3 right = cam->GetRight();

            const XMVECTOR offset =
                XMVectorAdd(XMVectorScale(XMLoadFloat3(&right), ox * amp),
                            XMVectorScale(XMLoadFloat3(&up),    oy * amp));

            XMFLOAT3 eye2, focus2;
            XMStoreFloat3(&eye2,   XMVectorAdd(XMLoadFloat3(&eye),   offset));
            XMStoreFloat3(&focus2, XMVectorAdd(XMLoadFloat3(&focus), offset));

            cam->SetLookAt(eye2, focus2, up);
        }
    }

    m_wasActive = true;
}

// CameraBrainSystem(R01b_CameraBrain) の後、PostProcessSystem(R09) の前に走る。
REGISTER_RENDER_SYSTEM(ScreenImpactSystem, Priority::RenderStage::R02_Environment);
