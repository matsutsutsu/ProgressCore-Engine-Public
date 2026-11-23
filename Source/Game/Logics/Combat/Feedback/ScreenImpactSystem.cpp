#include "ScreenImpactSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Core/Math/StringHash.h"

using namespace CCL::ECS;

namespace
{
    // スロットの識別子と優先度。エンジンは演出の中身を知らないので、名前と優先度は使う側が決める
    // ジャスト回避の演出（20）より低くして、同時に出たらジャスト回避を優先する
    const uint32_t kSource   = CCL::Utils::HashString("ScreenImpact");
    constexpr int  kPriority = 10;
}

void ScreenImpactSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 演出がない間は自分の枠を空ける
    // 2. 残り時間を進めて強さを求める
    // 3. 放射ブラーと色収差の枠へ書く

    auto *impact    = m_world->GetSingletonComponent<ScreenImpactComponent>();
    auto *radial    = m_world->GetSingletonComponent<RadialBlurConfigComponent>();
    auto *chromatic = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>();

    // ---------------------------------------------------------
    // 1. 演出がない間
    // ---------------------------------------------------------
    // weight を 0 にするのではなく枠ごと空け、他の演出に譲る
    //
    // 確保していない枠への Release は何もしないので、毎フレーム呼んでよい
    // 「前のフレームで出していたか」を覚えておく必要がない
    if (impact == nullptr || impact->timer <= 0.0f)
    {
        if (radial) radial->overrides.Release(kSource);
        if (chromatic) chromatic->overrides.Release(kSource);
        return;
    }

    // ---------------------------------------------------------
    // 2. 残り時間から強さを求める
    // ---------------------------------------------------------
    // dt は実時間。ヒットストップでゲームの時間が止まっている間も演出は進む
    impact->timer -= dt;
    if (impact->timer < 0.0f) impact->timer = 0.0f;

    const float t        = (impact->duration > 0.0f) ? (impact->timer / impact->duration) : 0.0f;
    const float strength = t * impact->power; // 命中した瞬間が最大で、残り時間に比例して弱くなる

    // ---------------------------------------------------------
    // 3. 自分の枠へ書く
    // ---------------------------------------------------------
    // 設定本体（enable / data）には触れない。合成は PostProcessSystem が行う
    auto *radialSlot = radial ? radial->overrides.Acquire(kSource, kPriority, radial->BaseParams()) : nullptr;
    if (radialSlot)
    {
        radialSlot->weight          = 1.0f;
        radialSlot->params.strength = impact->radialStrength * strength;
    }

    auto *chromaticSlot =
        chromatic ? chromatic->overrides.Acquire(kSource, kPriority, chromatic->BaseParams()) : nullptr;
    if (chromaticSlot)
    {
        chromaticSlot->weight          = 1.0f;
        chromaticSlot->params.strength = impact->chromaticStrength * strength;
    }
}



// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(ScreenImpactSystem, Priority::RenderStage::R02_Environment);
