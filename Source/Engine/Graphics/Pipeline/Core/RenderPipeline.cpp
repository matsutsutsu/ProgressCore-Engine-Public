#include "RenderPipeline.h"

#include <d3d11_1.h> // ID3DUserDefinedAnnotation
#include <wrl/client.h>
#include <Windows.h> // MultiByteToWideChar

// パス登録用
#include "Engine/Graphics/Pipeline/SetupPass.h"
#include "Engine/Graphics/Pipeline/ShadowPass.h"
#include "Engine/Graphics/Pipeline/OpaquePass.h"
#include "Engine/Graphics/Pipeline/SkyboxPass.h"
#include "Engine/Graphics/Pipeline/TransparentPass.h"
#include "Engine/Graphics/Pipeline/ParticlePass.h"
#include "Engine/Graphics/Pipeline/PrimitivePass.h"
#include "Engine/Graphics/Pipeline/PostProcessPass.h"

void RenderPipeline::Initialize(const RenderServices& services)
{
    _services = services;
}

void RenderPipeline::BuildDefaultPipeline()
{

    // 描画パスを実行順に登録
    AddPass<SetupPass>();       // 1. HDR バッファのクリアとバインド
    AddPass<ShadowPass>();      // 2. シャドウマップ生成
    AddPass<OpaquePass>();      // 3. 不透明ジオメトリ
    AddPass<SkyboxPass>();      // 4. スカイボックス
    AddPass<TransparentPass>(); // 5. 半透明
    AddPass<ParticlePass>();    // 6. パーティクル
    AddPass<PrimitivePass>();   // 7. デバッグプリミティブ
    AddPass<PostProcessPass>(); // 8. ポストプロセス・最終出力

}

void RenderPipeline::Shutdown()
{
    // 追加と逆順で Shutdown（後から登録したパスが先に依存解放される設計）
    for (auto it = _passes.rbegin(); it != _passes.rend(); ++it) {
        (*it)->Shutdown();
    }
    _passes.clear();
}

void RenderPipeline::Execute(const RenderFrameContext& ctx)
{
    if (!ctx.dc) return;

#if defined(ENABLE_EDITOR)
    // GPU マーカー：Pix / RenderDoc 上でパス境界を識別可能にする。
    // 取得失敗時はマーカーなしで続行（リリースモードと同じ動作）。
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    ctx.dc->QueryInterface(IID_PPV_ARGS(annotation.GetAddressOf()));
#endif

    for (auto& pass : _passes) {
        if (!pass->enabled) continue;

#if defined(ENABLE_EDITOR)
        if (annotation) {
            wchar_t wname[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, pass->GetName(), -1, wname, 64);
            annotation->BeginEvent(wname);
        }
#endif

        pass->Execute(ctx);

#if defined(ENABLE_EDITOR) 
        if (annotation) {
            annotation->EndEvent();
        }
#endif
    }
}

void RenderPipeline::OnResize(uint32_t width, uint32_t height)
{
    for (auto& pass : _passes) {
        pass->OnResize(width, height);
    }
}