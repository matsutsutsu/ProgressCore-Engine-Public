#include "PostProcessSystem.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"
#include "Engine/Graphics/Shader/PostProcess/BloomEffect.h"
#include "Engine/Graphics/Shader/PostProcess/DistortionEffect.h"
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
#include "Engine/Graphics/Shader/PostProcess/ContactShadowEffect.h"
#include "Engine/Graphics/Components/PostProcess/ContactShadowConfigComponent.h"
#include "Engine/Graphics/Shader/PostProcess/PauseBlurEffect.h"
#include "Engine/Core/App/GameFlowState.h" // ポーズ中だけ背景をぼかすため

// システムの実行順序の定義ヘッダー
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp(): フレームレート非依存の追従

void PostProcessSystem::Update(float dt)
{
    // Worldからリソースとして取得する
    if (!m_world->HasResource<PostProcessManager *>()) return;
    auto *postProcess = m_world->GetResource<PostProcessManager *>();

    // -------------------------------------------------------
    // 1. デフォルト値でリセット
    // -------------------------------------------------------
    // エンティティが一つもない場合、ブルームはOFF、露出は標準(1.0)にする
    BloomConfigComponent finalBloom;
    finalBloom.enable = false;

    ToneMapConfigComponent finalToneMap;
    finalToneMap.data.exposure = 1.0f;

    // -------------------------------------------------------
    // 2. BloomConfigComponent を検索
    // -------------------------------------------------------
    // ※ 本来は「GlobalVolume」タグやカメラ位置判定を行うが、
    //    ここではシンプルに「有効になっているものが見つかったら採用」する
    if (auto *comp = m_world->GetSingletonComponent<BloomConfigComponent>())
    {

        // 有効な設定が見つかったら上書き（後勝ち）
        if (comp && comp->enable)
        {
            finalBloom = *comp;
        }
    }

    // -------------------------------------------------------
    // 3. ToneMapConfigComponent を検索
    // -------------------------------------------------------
    if (auto *comp = m_world->GetSingletonComponent<ToneMapConfigComponent>())
    {
        finalToneMap = *comp;
    }

    // -------------------------------------------------------
    // 4. マネージャーへ一括転送
    // -------------------------------------------------------
    // 重要: SetEnabled() は「コンポーネントが見つかった時だけ」ではなく、
    // 見つからない場合も含めて毎フレーム無条件で呼ぶこと。
    // PostProcessManager のエフェクトはアプリ全体で使い回されるシングルトンなので、
    // 「コンポーネントが無いシーンでは何もしない」実装だと、以前別のシーンで
    // ONにしたエフェクトが次のシーンに持ち越されたまま(sticky)残ってしまう
    // (実際に発生：ポストエフェクト用コンポーネントの無い旧シーンで、以前有効化
    //  されたエフェクトが動き続けて負荷が高くなっていた)。
    // 「コンポーネント無し = 無効」を毎フレーム明示するのが唯一の正しい実装。

    if (auto *ssao = postProcess->GetEffect<SSAOEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<SSAOConfigComponent>();
        ssao->SetEnabled(comp && comp->enable);
        if (comp) ssao->UpdateParams(comp->data);
    }

    if (auto *contact = postProcess->GetEffect<ContactShadowEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<ContactShadowConfigComponent>();
        contact->SetEnabled(comp && comp->enable);
        if (comp) contact->UpdateParams(comp->data);
    }

    if (auto *distortion = postProcess->GetEffect<DistortionEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<DistortionConfigComponent>();
        distortion->SetEnabled(comp && comp->enable);
        if (comp)
        {
            comp->data.time += dt; // システム側でアニメーション時間を進める
            distortion->UpdateParams(comp->data);
        }
    }

    // 放射ブラー / 色収差は複数の演出システムが同時に要求しうる。
    // 各システムは自分の burst スロットへ書くだけで、最終値の決定はここに集約する。
    // enable(素の設定)が false でも演出は出す必要があるため、パスのON/OFFは
    // 合成後の強度が 0 かどうかで判断する。
    if (auto *radial = postProcess->GetEffect<RadialBlurEffect>())
    {
        auto       *comp     = m_world->GetSingletonComponent<RadialBlurConfigComponent>();
        const float strength = comp ? comp->ResolvedStrength() : 0.0f;
        radial->SetEnabled(strength > 0.0f);
        if (comp)
        {
            auto data         = comp->data;
            data.blurStrength = strength;
            radial->UpdateParams(data);
        }
    }

    if (auto *dof = postProcess->GetEffect<DepthOfFieldEffect>())
    {
        auto *comp = m_world->GetSingletonComponent<DepthOfFieldConfigComponent>();
        dof->SetEnabled(comp && comp->enable);
        if (comp) dof->UpdateParams(comp->data);
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
            // 脈動の位相は演出ではなく常に進む連続値。ここだけが所有者。
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

    // ポーズ中の背景ぼかし。
    //   世界の時計は止まっていても、このSystemは描画ステージなので実時間の dt が来る。
    //   だから「止まっている間に演出が進む」ことができ、パッと切り替わらずに沈み込む。
    if (auto *blur = postProcess->GetEffect<PauseBlurEffect>())
    {
        const bool paused = m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().paused;

        // 目標へ指数で寄せる。ポーズ解除は入りより速くして、操作再開を待たせない。
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