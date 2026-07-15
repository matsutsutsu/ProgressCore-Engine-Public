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

#include <DirectXMath.h>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;

void ScreenImpactSystem::Update(float dt)
{
    auto* impact = _world->GetSingletonComponent<ScreenImpactComponent>();

    const bool active = (impact && impact->timer > 0.0f);

    // ── PP設定への書き込みヘルパー（設定 singleton が無ければ何もしない） ──
    auto writeRadial = [&](bool en, float strength) {
        if (auto* c = _world->GetSingletonComponent<RadialBlurConfigComponent>())
        {
            c->enable          = en;
            c->data.blurCenter = XMFLOAT2(0.5f, 0.5f);
            c->data.blurStrength = strength;
            c->data.blurFalloff  = 2.0f;
        }
    };
    auto writeChroma = [&](bool en, float strength) {
        if (auto* c = _world->GetSingletonComponent<ChromaticAberrationConfigComponent>())
        {
            c->enable            = en;
            c->data.chromaCenter = XMFLOAT2(0.5f, 0.5f);
            c->data.chromaStrength = strength;
            c->data.chromaFalloff  = 2.0f;
        }
    };

    if (!active)
    {
        // 立ち下がり: 一度だけ PP を OFF にして手放す（以後は触らない）。
        if (_wasActive)
        {
            writeRadial(false, 0.0f);
            writeChroma(false, 0.0f);
            _wasActive = false;
        }
        return;
    }

    // ── 1. 減衰 ──
    impact->timer -= dt;
    if (impact->timer < 0.0f) impact->timer = 0.0f;

    const float t        = (impact->duration > 0.0f) ? (impact->timer / impact->duration) : 0.0f;
    const float strength = t * impact->power; // 命中直後=最大 → 0 へ

    // ── 2. PPバースト（放射ブラー＋色収差） ──
    writeRadial(true, impact->radialStrength * strength);
    writeChroma(true, impact->chromaticStrength * strength);

    // ── 3. カメラシェイク（CameraBrain 確定後の MainCamera に重ねる） ──
    if (_world->HasResource<Camera*>())
    {
        if (Camera* cam = _world->GetResource<Camera*>())
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

    _wasActive = true;
}

// CameraBrainSystem(R01b_CameraBrain) の後、PostProcessSystem(R09) の前に走る。
REGISTER_RENDER_SYSTEM(ScreenImpactSystem, Priority::RenderStage::R02_Environment);
