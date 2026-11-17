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
    // 1. 演出がない間は自分のスロットを手放す
    // 2. 残り時間を進めて強さを求める
    // 3. 放射ブラーと色収差のスロットへ書く

    auto *impact = m_world->GetSingletonComponent<ScreenImpactComponent>();

    // ---------------------------------------------------------
    // 1. 演出がない間
    // ---------------------------------------------------------
    // weight を 0 にするのではなくスロットごと手放し、他の演出に枠を空ける
    //
    // 確保していないスロットへの Release は何もしないので、毎フレーム呼んでよい
    // 「前のフレームで出していたか」を覚えておく必要がない
    if (impact == nullptr || impact->timer <= 0.0f)
    {
        CCL::PostFx::ReleaseAll(*m_world, kSource);
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
    // 3. 自分のスロットへ書く
    // ---------------------------------------------------------
    // 設定本体（enable / data）には触れない。合成は PostProcessSystem が行う
    if (auto *ov = CCL::PostFx::RadialBlur(*m_world, kSource, kPriority))
    {
        ov->weight          = 1.0f;
        ov->params.strength = impact->radialStrength * strength;
    }

    if (auto *ov = CCL::PostFx::ChromaticAberration(*m_world, kSource, kPriority))
    {
        ov->weight          = 1.0f;
        ov->params.strength = impact->chromaticStrength * strength;
    }
}



// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(ScreenImpactSystem, Priority::RenderStage::R02_Environment);
