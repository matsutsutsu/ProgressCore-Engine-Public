#include "JustEvadeEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h"
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"

#include "Engine/Core/Math/StringHash.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    // スロットの識別子と優先度。エンジンは演出の中身を知らないので、名前と優先度は使う側が決める。
    // 命中の演出（10）より高くして、同時に出たらジャスト回避の演出を優先する。
    const uint32_t kSource   = CCL::Utils::HashString("JustEvade");
    constexpr int  kPriority = 20;

    // current を target へ rate(=dt/所要時間) の歩幅で線形に近づける（オーバーシュートしない）。
    inline float MoveToward(float current, float target, float rate)
    {
        if (rate <= 0.0f) return target;
        if (current < target) return (std::min)(current + rate, target);
        return (std::max)(current - rate, target);
    }
} // namespace

std::vector<CCL::ECS::TypeID> JustEvadeEffectSystem::GetReadTypes() const
{
    return {TypeInfo<JustEvadeEffectComponent>::ID()};
}

std::vector<CCL::ECS::TypeID> JustEvadeEffectSystem::GetWriteTypes() const
{
    // 触るポストプロセスはビネットだけ（演出をビネット1本へ集約した）。
    return {TypeInfo<VignetteConfigComponent>::ID()};
}

void JustEvadeEffectSystem::Update(float dt)
{
    auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>();
    if (!fx) return;

    auto *vig = m_world->GetSingletonComponent<VignetteConfigComponent>();

    // active に応じて weight を実時間でフェードする。
    //   立てている間（active）は fadeInTime でピーク(1)へ、降ろすと fadeOutTime で 0 へ。
    //   dt は実時間なので、世界スロー中でも演出のフェード速度は一定。
    const bool  active   = (fx->flurryActive || fx->cleaveActive);
    const float target   = active ? 1.0f : 0.0f;
    const float fadeTime = active ? fx->fadeInTime : fx->fadeOutTime;
    const float rate     = (fadeTime > 0.0001f) ? (dt / fadeTime) : 1.0f;
    fx->weight           = MoveToward(fx->weight, target, rate);

    // 完全に消えたフレームで、自分のスロットだけを手放す（他の演出・設定本体には触れない）。
    const bool visible = (fx->weight > 0.001f);
    if (!visible)
    {
        if (m_wasActive && vig) vig->overrides.Release(kSource);
        m_wasActive = false;
        return;
    }

    // ビネット（周囲が暗くなる）。weight を 0→1 で上げることで徐々に暗くする。
    //   最終的な合成と描画の有無は PostProcessSystem(R09) が決める。
    if (vig)
    {
        if (auto *ov = vig->overrides.Acquire(kSource, kPriority))
        {
            ov->weight            = fx->weight;            // 0→1 で徐々に暗く
            ov->params.intensity  = fx->vignetteIntensity; // ピークの暗さ
            ov->params.color      = {0.0f, 0.0f, 0.0f, 1.0f};
            ov->params.pulseSpeed = 0.0f;                  // 明滅なし
            ov->params.radius     = fx->vignetteRadius;    // 半径
            ov->params.softness   = vig->data.vigSoftness; // 縁のぼかしはシーン設定を踏襲
        }
    }

    m_wasActive = true;
}

// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(JustEvadeEffectSystem, Priority::RenderStage::R02_Environment);
