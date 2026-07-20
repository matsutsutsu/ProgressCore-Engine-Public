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
    // ★重要: SetEnabled() は「コンポーネントが見つかった時だけ」ではなく、
    // 見つからない場合も含めて毎フレーム無条件で呼ぶこと。
    // PostProcessManager のエフェクトはアプリ全体で使い回されるシングルトンなので、
    // 「コンポーネントが無いシーンでは何もしない」実装だと、以前別のシーンで
    // ONにしたエフェクトが次のシーンに持ち越されたまま(sticky)残ってしまう
    // (実際に発生：ポストエフェクト用コンポーネントの無い旧シーンで、以前有効化
    //  されたエフェクトが動き続けて負荷が高くなっていた)。
    // 「コンポーネント無し = 無効」を毎フレーム明示するのが唯一の正しい実装。

    if (auto* ssao = postProcess->GetEffect<SSAOEffect>()) {
      auto* comp = _world->GetSingletonComponent<SSAOConfigComponent>();
      ssao->SetEnabled(comp && comp->enable);
      if (comp) ssao->UpdateParams(comp->data);
    }

    if (auto* contact = postProcess->GetEffect<ContactShadowEffect>()) {
      auto* comp = _world->GetSingletonComponent<ContactShadowConfigComponent>();
      contact->SetEnabled(comp && comp->enable);
      if (comp) contact->UpdateParams(comp->data);
    }

    if (auto *distortion = postProcess->GetEffect<DistortionEffect>()) {
      auto* comp = _world->GetSingletonComponent<DistortionConfigComponent>();
      distortion->SetEnabled(comp && comp->enable);
      if (comp) {
        comp->data.time += dt; // システム側でアニメーション時間を進める
        distortion->UpdateParams(comp->data);
      }
    }

    if (auto* radial = postProcess->GetEffect<RadialBlurEffect>()) {
      auto* comp = _world->GetSingletonComponent<RadialBlurConfigComponent>();
      radial->SetEnabled(comp && comp->enable);
      if (comp) radial->UpdateParams(comp->data);
    }

    if (auto* dof = postProcess->GetEffect<DepthOfFieldEffect>()) {
      auto* comp = _world->GetSingletonComponent<DepthOfFieldConfigComponent>();
      dof->SetEnabled(comp && comp->enable);
      if (comp) dof->UpdateParams(comp->data);
    }

    if (auto* bloom = postProcess->GetEffect<BloomEffect>()) {
        bloom->SetEnabled(finalBloom.enable);
        bloom->UpdateParams(finalBloom.data);
    }

    if (auto* chroma = postProcess->GetEffect<ChromaticAberrationEffect>()) {
      auto* comp = _world->GetSingletonComponent<ChromaticAberrationConfigComponent>();
      chroma->SetEnabled(comp && comp->enable);
      if (comp) chroma->UpdateParams(comp->data);
    }

    if (auto* vignette = postProcess->GetEffect<VignetteEffect>()) {
      auto* comp = _world->GetSingletonComponent<VignetteConfigComponent>();
      vignette->SetEnabled(comp && comp->enable);
      if (comp) {
        comp->data.vigTime += dt;
        vignette->UpdateParams(comp->data);
      }
    }

    if (auto* grading = postProcess->GetEffect<ColorGradingEffect>()) {
      auto* comp = _world->GetSingletonComponent<ColorGradingConfigComponent>();
      grading->SetEnabled(comp && comp->enable);
      if (comp) grading->UpdateParams(comp->data);
    }

    postProcess->toneMapConfig = finalToneMap;
}


REGISTER_RENDER_SYSTEM(PostProcessSystem, Priority::RenderStage::R09_PostProcess);