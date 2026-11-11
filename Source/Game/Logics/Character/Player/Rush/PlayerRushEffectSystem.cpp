#include "PlayerRushEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Components/PostProcess/PostFx.h"

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
} // namespace



std::vector<CCL::ECS::TypeID> PlayerRushEffectSystem::GetWriteTypes() const
{
    // 値を書くのはビネットと放射ブラーだけだが、後始末の PostFx::ReleaseAll は
    // 上書きを受け付けるすべての設定へ書くので、全部を申告する
    auto types = Base::GetWriteTypes();
    for (const TypeID id : CCL::PostFx::AllWriteTypes()) types.push_back(id);
    return types;
}



void PlayerRushEffectSystem::Update(float dt)
{
    // 処理フロー:
    // 1. フェードの重みを実時間で目標へ寄せる
    // 2. 消えている間は、自分のスロットを手放して終える
    // 3. 見えている間は、ビネットへ書く
    // 4. 同じ重みで放射ブラーへ書く

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

        // 自分のスロットだけを手放し、ほかの演出と設定本体には触れない
        // 確保していないスロットの Release は何もしないので、毎フレーム呼んでよい
        const bool visible = (effect.weight > 0.001f);
        if (!visible)
        {
            CCL::PostFx::ReleaseAll(*m_world, kSource);
            return;
        }

        // ---------------------------------------------------------
        // 3. ビネット
        // ---------------------------------------------------------

        // 縁のぼかし（softness）は書かない
        // スロットにはシーンの設定が土台に入っているので、書かない項目はシーンの見た目のまま
        if (auto *ov = CCL::PostFx::Vignette(*m_world, kSource, kPriority))
        {
            ov->weight            = effect.weight;
            ov->params.intensity  = effect.vignetteIntensity;
            ov->params.radius     = effect.vignetteRadius;
            ov->params.color      = {0.0f, 0.0f, 0.0f, 1.0f};
            ov->params.pulseSpeed = 0.0f; // 明滅させない
        }

        // ---------------------------------------------------------
        // 4. 放射ブラー
        // ---------------------------------------------------------

        // 中心は書かず、シーンの設定のままにする
        if (auto *ov = CCL::PostFx::RadialBlur(*m_world, kSource, kPriority))
        {
            ov->weight          = effect.weight;
            ov->params.strength = effect.radialStrength;
            ov->params.falloff  = effect.radialFalloff;
        }
    });
}



// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(PlayerRushEffectSystem, Priority::RenderStage::R02_Environment);
