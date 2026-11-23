#include "PlayerRushEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"

#include "Engine/Core/Math/StringHash.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    // ポストエフェクトの上書きスロットの識別子と優先度
    // 命中の演出より高くして、同時に出たらこちらを優先する
    const uint32_t kSource   = CCL::Utils::HashString("JustEvade");
    constexpr int  kPriority = 20;

    // current を target へ rate だけ近づける。target を通り越さない
    // rate が 0 以下なら target を返す
    inline float MoveToward(float current, float target, float rate)
    {
        if (rate <= 0.0f) return target;
        if (current < target) return (std::min)(current + rate, target);
        return (std::max)(current - rate, target);
    }
}



std::vector<CCL::ECS::TypeID> PlayerRushEffectSystem::GetWriteTypes() const
{
    // ビネットと放射ブラーの上書きの枠へ書く
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<VignetteConfigComponent>::ID());
    types.push_back(TypeInfo<RadialBlurConfigComponent>::ID());
    return types;
}



void PlayerRushEffectSystem::Update(float dt)
{
    // 処理フロー:
    // 1. フェードの重みを実時間で目標へ寄せる
    // 2. 消えている間は、自分の枠を空けて終える
    // 3. 見えている間は、ビネットへ書く
    // 4. 同じ重みで放射ブラーへ書く

    // シーンに設定がなければ nullptr。その演出だけ出さない
    auto *vignette = m_world->GetSingletonComponent<VignetteConfigComponent>();
    auto *radial   = m_world->GetSingletonComponent<RadialBlurConfigComponent>();

    ForEach([&](PlayerRushEffectComponent &effect, const PlayerRushComponent &rush) {

        // ---------------------------------------------------------
        // 1. フェード
        // ---------------------------------------------------------

        // dt は実時間なので、世界のスロー中もフェードの速さは変わらない
        // ラッシュ中と、範囲外の回避の短いスロー中に出す。どちらも終えたステップから消え始める
        const bool  active   = rush.isActive || rush.evadeSlowTimer > 0.0f;
        const float target   = active ? 1.0f : 0.0f;
        const float fadeTime = active ? effect.fadeInTime : effect.fadeOutTime;
        const float rate     = (fadeTime > 0.0001f) ? (dt / fadeTime) : 1.0f;

        effect.weight = MoveToward(effect.weight, target, rate);

        // ---------------------------------------------------------
        // 2. 消えている間の後始末
        // ---------------------------------------------------------

        // 自分の枠だけを空け、ほかの演出と設定本体には触れない
        // 確保していない枠の Release は何もしないので、毎フレーム呼んでよい
        const bool visible = (effect.weight > 0.001f);
        if (!visible)
        {
            if (vignette) vignette->overrides.Release(kSource);
            if (radial) radial->overrides.Release(kSource);
            return;
        }

        // ---------------------------------------------------------
        // 3. ビネット
        // ---------------------------------------------------------

        // 縁のぼかし（softness）は書かない
        // 枠にはシーンの設定が土台に入っているので、書かない項目はシーンの見た目のまま
        auto *vigSlot = vignette ? vignette->overrides.Acquire(kSource, kPriority, vignette->BaseParams()) : nullptr;
        if (vigSlot)
        {
            vigSlot->weight            = effect.weight;
            vigSlot->params.intensity  = effect.vignetteIntensity;
            vigSlot->params.radius     = effect.vignetteRadius;
            vigSlot->params.color      = {0.0f, 0.0f, 0.0f, 1.0f};
            vigSlot->params.pulseSpeed = 0.0f; // 明滅させない
        }

        // ---------------------------------------------------------
        // 4. 放射ブラー
        // ---------------------------------------------------------

        // 中心は書かず、シーンの設定のままにする
        auto *radialSlot = radial ? radial->overrides.Acquire(kSource, kPriority, radial->BaseParams()) : nullptr;
        if (radialSlot)
        {
            radialSlot->weight          = effect.weight;
            radialSlot->params.strength = effect.radialStrength;
            radialSlot->params.falloff  = effect.radialFalloff;
        }
    });
}



// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(PlayerRushEffectSystem, Priority::RenderStage::R02_Environment);
