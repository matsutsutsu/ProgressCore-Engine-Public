#include "UIProgressBarCollectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/UI/ECS/UIDrawList.h"

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;

void UIProgressBarCollectSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<UIDrawList>()) return;

    auto &drawList = m_world->GetResource<UIDrawList>();

    ForEach([&](const UITransformComponent &t, const UIProgressBarComponent &bar) {
        if (!bar.visible) return;

        const float v = std::clamp(bar.value, 0.0f, 1.0f);

        // 被弾の揺れ。ゲージ全体の位置をずらす。
        //   X と Y で振動数をずらしているのは、同じにすると斜め 45 度の直線往復になり、
        //   揺れではなくスライドに見えるため（軌跡をリサージュ曲線にする）
        DirectX::XMFLOAT2 origin = t.computedPos;
        if (bar.shake > 0.0f)
        {
            const float amp   = bar.shakeAmplitude * bar.shake * bar.shake;
            const float phase = bar.shakeElapsed * bar.shakeFrequency * 6.2831853f;
            origin.x += std::sin(phase) * amp;
            origin.y += std::sin(phase * 1.7f) * amp * 0.5f; // 縦は半分にして、横長のバーが跳ねて見えないようにする
        }

        // ゲージ1本を Gauge アイテム1つにまとめる。形・縁・遅延バー・区切りなどはシェーダーが描く
        GpuData::CbUIGaugeData g{};
        g.slantPx     = std::max(0.0f, bar.slant);
        g.value       = v;
        g.trailValue  = bar.showTrail ? std::clamp(bar.trailValue, 0.0f, 1.0f) : 0.0f; // 描かないときはフィルの裏に隠れる 0 にする
        g.segments    = bar.segments;
        g.gapPx       = bar.segmentGap;
        g.frameWidth  = std::max(0.0f, bar.frame);
        g.glowWidth   = bar.glowWidth;
        g.flash       = bar.hitFlash * bar.shake * bar.shake; // 揺れと同じ減り方にして、当たった瞬間だけ白く飛ばす
        g.time        = bar.animTime;
        g.marker      = bar.marker;
        g.markerWidth = bar.markerWidth;
        g.sheen       = bar.sheen;
        g.gradient    = std::clamp(bar.gradient, 0.0f, 1.0f);
        g.bgColor     = bar.bgColor;
        g.fillColor   = bar.fillColor;
        g.trailColor  = bar.trailColor;
        g.frameColor  = bar.frameColor;
        g.glowColor   = bar.glowColor;

        // 演出の強さと進み具合
        g.hitAge    = bar.hitAge;
        g.healFrom  = std::clamp(bar.healFrom, 0.0f, 1.0f);
        g.healAge   = bar.healAge;
        g.glitch    = std::clamp(bar.glitch, 0.0f, 1.0f);
        g.auraWidth = bar.auraWidth;
        g.auraColor = {bar.auraColor.x, bar.auraColor.y, bar.auraColor.z,
                       bar.auraColor.w * std::clamp(bar.auraLevel, 0.0f, 1.0f)};

        UIDrawItem item;
        item.kind      = UIDrawKind::Gauge;
        item.pos       = origin;
        item.size      = t.computedSize;
        item.color     = {1.0f, 1.0f, 1.0f, t.alpha}; // 出現アニメ等の不透明度
        item.sortOrder = bar.sortOrder;
        item.gauge     = g;
        drawList.items.push_back(std::move(item));
    });
}

REGISTER_RENDER_SYSTEM(UIProgressBarCollectSystem, Priority::RenderStage::R10b_UICollect);
