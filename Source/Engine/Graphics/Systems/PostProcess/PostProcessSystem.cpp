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



// システムの実行順序の定義ヘッダー
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void PostProcessSystem::Update(float dt) {
    // Worldからリソースとして取得する
    if (!_world->HasResource<PostProcessManager *>()) return;
    auto *postProcess = _world->GetResource<PostProcessManager *>();

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
    if(auto* comp = _world->GetSingletonComponent<BloomConfigComponent>()) {

        // 有効な設定が見つかったら上書き（後勝ち）
        if (comp && comp->enable) {
            finalBloom = *comp;
        }
    }

    // -------------------------------------------------------
    // 3. ToneMapConfigComponent を検索
    // -------------------------------------------------------
    if (auto* comp = _world->GetSingletonComponent<ToneMapConfigComponent>()) {      
        finalToneMap = *comp;
    }

    // -------------------------------------------------------
    // 4. マネージャーへ一括転送
    // -------------------------------------------------------

    // ★ Distortion の転送処理を追加
    if (auto *comp =
            _world->GetSingletonComponent<DistortionConfigComponent>()) {
      if (auto *distortion = postProcess->GetEffect<DistortionEffect>()) {
        distortion->SetEnabled(comp->enable);
        comp->data.time += dt; // システム側でアニメーション時間を進める
        distortion->UpdateParams(comp->data);
      }
    }

    if (auto* comp = _world->GetSingletonComponent<RadialBlurConfigComponent>()) {
      if (auto* radial = postProcess->GetEffect<RadialBlurEffect>()) {
        radial->SetEnabled(comp->enable);
        radial->UpdateParams(comp->data);
      }
    }

    if (auto* comp = _world->GetSingletonComponent<DepthOfFieldConfigComponent>()) {
      if (auto* dof = postProcess->GetEffect<DepthOfFieldEffect>()) {
        dof->SetEnabled(comp->enable);
        dof->UpdateParams(comp->data);
      }
    }

    if (auto* bloom = postProcess->GetEffect<BloomEffect>()) {
        bloom->SetEnabled(finalBloom.enable);
        bloom->UpdateParams(finalBloom.data);
    }

    if (auto* comp = _world->GetSingletonComponent<ChromaticAberrationConfigComponent>()) {
      if (auto* chroma = postProcess->GetEffect<ChromaticAberrationEffect>()) {
        chroma->SetEnabled(comp->enable);
        chroma->UpdateParams(comp->data);
      }
    }

    if (auto* comp = _world->GetSingletonComponent<VignetteConfigComponent>()) {
      if (auto* vignette = postProcess->GetEffect<VignetteEffect>()) {
        vignette->SetEnabled(comp->enable);
        comp->data.vigTime += dt;
        vignette->UpdateParams(comp->data);
      }
    }

    if (auto* comp = _world->GetSingletonComponent<ColorGradingConfigComponent>()) {
      if (auto* grading = postProcess->GetEffect<ColorGradingEffect>()) {
        grading->SetEnabled(comp->enable);
        grading->UpdateParams(comp->data);
      }
    }

    postProcess->toneMapConfig = finalToneMap;
}


REGISTER_RENDER_SYSTEM(PostProcessSystem, Priority::RenderStage::R09_PostProcess);