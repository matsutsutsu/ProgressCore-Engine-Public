#include "PostProcessSystem.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"
#include "Engine/Graphics/Shader/PostProcess/BloomEffect.h"
#include "Engine/Graphics/Shader/PostProcess/RadialBlurEffect.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Shader/PostProcess/DepthOfFieldEffect.h"
#include "Engine/Graphics/Components/PostProcess/DepthOfFieldConfigComponent.h"
#include "Engine/Graphics/Shader/PostProcess/VignetteEffect.h"
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Graphics/Shader/PostProcess/ChromaticAberrationEffect.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Graphics/Shader/PostProcess/ColorGradingEffect.h"
#include "Engine/Graphics/Components/PostProcess/ColorGradingConfigComponent.h"
#include "Engine/Graphics/Shader/PostProcess/SSAOEffect.h"
#include "Engine/Graphics/Components/PostProcess/SSAOConfigComponent.h"
#include "Engine/Graphics/Shader/PostProcess/PauseBlurEffect.h"
#include "Engine/Core/App/GameFlowState.h"

// システムの実行順序の定義ヘッダー
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h"

void PostProcessSystem::Update(float dt)
{
    if (!m_world->HasResource<PostProcessManager *>()) return;
    auto *postProcess = m_world->GetResource<PostProcessManager *>();

    // -------------------------------------------------------
    // 1. 既定値。コンポーネントがなければブルームはオフ、露出は 1.0
    // -------------------------------------------------------
    BloomConfigComponent finalBloom;
    finalBloom.enable = false;

    ToneMapConfigComponent finalToneMap;
    finalToneMap.data.exposure = 1.0f;

    // -------------------------------------------------------
    // 2. ブルーム（シングルトン）。有効なら採用する
    // -------------------------------------------------------
    if (auto *comp = m_world->GetSingletonComponent<BloomConfigComponent>(); comp && comp->enable)
    {
        finalBloom = *comp;
    }

    // -------------------------------------------------------
    // 3. トーンマッピング
    // -------------------------------------------------------
    if (auto *comp = m_world->GetSingletonComponent<ToneMapConfigComponent>())
    {
        finalToneMap = *comp;
    }

    // -------------------------------------------------------
    // 4. 各エフェクトへ渡す
    // -------------------------------------------------------
    // SetEnabled は、コンポーネントがないときも含めて毎フレーム呼ぶ
    // エフェクトはアプリ全体で使い回すので、ないときに何もしないと、前のシーンでオンにしたエフェクトが動き続ける

    if (auto *ssao = postProcess->GetEffect<SSAOEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<SSAOConfigComponent>();
        ssao->SetEnabled(comp && comp->enable);
        if (comp) ssao->UpdateParams(comp->data);
    }

    // 放射状ブラーと色収差は、複数の演出が同時に求めることがある
    // 各演出は自分のスロットに書くだけで、最終的な値はここで決める
    // enable（普段の設定）が false でも演出は出すので、パスのオン/オフは合成後の強さが 0 かどうかで決める
    if (auto *radial = postProcess->GetEffect<RadialBlurEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<RadialBlurConfigComponent>();
        if (comp)
        {
            const auto data = comp->Resolve();
            radial->SetEnabled(data.blurStrength > 0.0f);
            radial->UpdateParams(data);
        }
        else
        {
            radial->SetEnabled(false);
        }
    }

    // 被写界深度は、ムービーのショットが一時的に上書きする
    // 普段の設定がオフでもショットはぼかすので、パスのオン/オフは合成後のぼけが 0 かどうかで決める
    if (auto *dof = postProcess->GetEffect<DepthOfFieldEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<DepthOfFieldConfigComponent>();
        if (comp)
        {
            const auto data = comp->Resolve();
            dof->SetEnabled(data.dofMaxBlur > 0.0f);
            dof->UpdateParams(data);
        }
        else
        {
            dof->SetEnabled(false);
        }
    }

    if (auto *bloom = postProcess->GetEffect<BloomEffect>())
    {
        bloom->SetEnabled(finalBloom.enable);
        bloom->UpdateParams(finalBloom.data);
    }

    if (auto *chroma = postProcess->GetEffect<ChromaticAberrationEffect>())
    {
        auto       *comp     = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>();
        const float strength = comp ? comp->ResolvedStrength() : 0.0f;
        chroma->SetEnabled(strength > 0.0f);
        if (comp)
        {
            auto data           = comp->data;
            data.chromaStrength = strength;
            chroma->UpdateParams(data);
        }
    }

    if (auto *vignette = postProcess->GetEffect<VignetteEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<VignetteConfigComponent>();
        vignette->SetEnabled(comp && comp->ResolvedEnable());
        if (comp)
        {
            // 脈動の時間は演出と関係なく常に進める。進めるのはここだけ
            comp->data.vigTime += dt;
            vignette->UpdateParams(comp->Resolve());
        }
    }

    if (auto *grading = postProcess->GetEffect<ColorGradingEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<ColorGradingConfigComponent>();
        grading->SetEnabled(comp && comp->ResolvedEnable());
        if (comp) grading->UpdateParams(comp->Resolve());
    }

    // ポーズ中の背景ぼかし
    //   世界の時計が止まっていても、この System は描画ステージなので実時間の dt が来る
    //   そのためポーズ中でもぼかしの量が徐々に変わり、一瞬で切り替わらない
    if (auto *blur = postProcess->GetEffect<PauseBlurEffect>())
    {
        const bool paused = m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().paused;

        // 目標へ指数的に寄せる。解除は入るときより速くして、操作の再開を待たせない
        const float target = paused ? 1.0f : 0.0f;
        const float speed  = paused ? kPauseBlurInSpeed : kPauseBlurOutSpeed;
        m_pauseBlur += (target - m_pauseBlur) * CCL::Math::Damp(speed, dt);
        if (!paused && m_pauseBlur < 0.002f) m_pauseBlur = 0.0f; // 端数で薄くぼけ続けないよう切る

        GpuData::CbPauseBlurData data{};
        data.pauseBlurRadius = m_pauseBlur * kPauseBlurMaxRadius;
        blur->UpdateParams(data);
        blur->SetEnabled(m_pauseBlur > 0.0f);
    }

    postProcess->toneMapConfig = finalToneMap;
}



REGISTER_RENDER_SYSTEM(PostProcessSystem, Priority::RenderStage::R09_PostProcess);