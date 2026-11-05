#pragma once
// ===========================================================================
// 概要: ポストプロセスの一時的な上書きを、演出の System から1行ずつ扱うための関数
//
// 備考: スロットの仕組みと、設定本体（enable / data）を直接書かない理由は PostEffectOverrides.h を参照
//       ReleaseAll は上書きを受け付ける全コンポーネントへ書くので、呼ぶ System は GetWriteTypes() で AllWriteTypes() を返す
//       申告より広く書くと、同じ設定を書く別の System と並列に走り、同じ空きスロットを奪い合う
//
// @code
//   static const uint32_t kSource   = CCL::Utils::HashString("MyEffect");
//   constexpr int         kPriority = 15;
//
//   // 出したいフレーム（毎フレーム呼んでよい）
//   if (auto *ov = CCL::PostFx::Vignette(*m_world, kSource, kPriority))
//   {
//       ov->weight           = env;  // 普段の設定からどれだけ寄せるか（0〜1）
//       ov->params.intensity = peak; // 変えたい項目だけ書く
//   }
//
//   // 終わったフレーム
//   CCL::PostFx::ReleaseAll(*m_world, kSource);
// @endcode
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ColorGradingConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/DepthOfFieldConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"

#include <cstdint>
#include <vector>

// ============================================================
// 上書きを受け付けるコンポーネントの一覧
//   X-Macro で一元定義する。エフェクトを増やすときはここに1行足せば、
//   ReleaseAll と AllWriteTypes の両方が追従する
// ============================================================
#define CCL_POSTFX_OVERRIDABLE_LIST(X)    \
    X(VignetteConfigComponent)            \
    X(ColorGradingConfigComponent)        \
    X(ChromaticAberrationConfigComponent) \
    X(RadialBlurConfigComponent)          \
    X(DepthOfFieldConfigComponent)

namespace CCL::PostFx
{
    // TConfig が持つスロットの型（PostEffectOverrides<...>::Slot）
    template <class TConfig>
    using SlotOf = typename decltype(TConfig::overrides)::Slot;

    // 発生源 source のスロットを返す。なければ確保し、普段の設定（data）を params の土台に入れる
    // 呼ぶ側は変えたい項目だけ書けばよい。同じ source なら同じスロットが返るので、毎フレーム呼んでよい
    // priority が大きいほど後に重なる。設定のコンポーネントがない・スロットが満杯なら nullptr
    template <class TConfig>
    SlotOf<TConfig> *Acquire(ECS::World &world, uint32_t source, int priority, const char *effectName)
    {
        TConfig *comp = world.GetSingletonComponent<TConfig>();
        if (comp == nullptr)
        {
            return nullptr;
        }

        // 土台を入れてよいのは新規確保のときだけ
        // 既に持っているスロットへ入れ直すと、前のフレームに書いた値が毎フレーム消える
        const bool isNew = (comp->overrides.Find(source) == nullptr);

        SlotOf<TConfig> *slot = comp->overrides.Acquire(source, priority);

        // 満杯は「演出が無言で出ない」という追いにくい症状になる。原因を名前で残す
        if (slot == nullptr)
        {
            CCL_LOG_WARN(LogCategory::Render,
                         "[PostFx] %s のスロットが満杯で確保できなかった（容量 %d）。この演出は出ない",
                         effectName, decltype(TConfig::overrides)::CAPACITY);
            return nullptr;
        }

        if (isNew)
        {
            slot->params = comp->BaseParams();
        }
        return slot;
    }

    // --- エフェクトごとの短縮版（Acquire の effectName を埋めたもの） ---

    inline SlotOf<VignetteConfigComponent> *Vignette(ECS::World &world, uint32_t source, int priority)
    {
        return Acquire<VignetteConfigComponent>(world, source, priority, "Vignette");
    }

    inline SlotOf<ColorGradingConfigComponent> *ColorGrading(ECS::World &world, uint32_t source, int priority)
    {
        return Acquire<ColorGradingConfigComponent>(world, source, priority, "ColorGrading");
    }

    inline SlotOf<ChromaticAberrationConfigComponent> *ChromaticAberration(ECS::World &world, uint32_t source,
                                                                           int priority)
    {
        return Acquire<ChromaticAberrationConfigComponent>(world, source, priority, "ChromaticAberration");
    }

    inline SlotOf<RadialBlurConfigComponent> *RadialBlur(ECS::World &world, uint32_t source, int priority)
    {
        return Acquire<RadialBlurConfigComponent>(world, source, priority, "RadialBlur");
    }

    inline SlotOf<DepthOfFieldConfigComponent> *DepthOfField(ECS::World &world, uint32_t source, int priority)
    {
        return Acquire<DepthOfFieldConfigComponent>(world, source, priority, "DepthOfField");
    }

    // --- 後始末 ---

    // 1つのエフェクトから自分のスロットを手放す（被弾のビネットと色収差のように、終わる時刻が違う演出で使う）
    template <class TConfig>
    void Release(ECS::World &world, uint32_t source)
    {
        if (TConfig *comp = world.GetSingletonComponent<TConfig>())
        {
            comp->overrides.Release(source);
        }
    }

    // 全部のエフェクトから自分のスロットを手放す。演出の終わりに1回呼べばよい
    // 呼ぶ System は GetWriteTypes で AllWriteTypes() を返す（ファイル先頭の備考）
    inline void ReleaseAll(ECS::World &world, uint32_t source)
    {
#define CCL_POSTFX_X(Comp) Release<Comp>(world, source);
        CCL_POSTFX_OVERRIDABLE_LIST(CCL_POSTFX_X)
#undef CCL_POSTFX_X
    }

    // ReleaseAll を使う System が GetWriteTypes() でそのまま返せる型一覧
    inline std::vector<ECS::TypeID> AllWriteTypes()
    {
#define CCL_POSTFX_X(Comp) ECS::TypeInfo<Comp>::ID(),
        return {CCL_POSTFX_OVERRIDABLE_LIST(CCL_POSTFX_X)};
#undef CCL_POSTFX_X
    }
} // namespace CCL::PostFx
