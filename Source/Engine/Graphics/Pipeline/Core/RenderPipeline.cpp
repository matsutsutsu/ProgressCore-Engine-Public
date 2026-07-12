#include "RenderPipeline.h"

#include <d3d11_1.h> // ID3DUserDefinedAnnotation
#include <wrl/client.h>
#include <Windows.h> // MultiByteToWideChar

#include "Engine/Graphics/RHI/Graphics.h"
#include "tracy/TracyD3D11.hpp"

// パス登録用
#include "Engine/Graphics/Pipeline/SetupPass.h"
#include "Engine/Graphics/Pipeline/ShadowPass.h"
#include "Engine/Graphics/Pipeline/OpaquePass.h"
#include "Engine/Graphics/Pipeline/SkyboxPass.h"
#include "Engine/Graphics/Pipeline/SceneColorGrabPass.h"
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
    AddPass<SceneColorGrabPass>(); // 4.5 屈折用シーンカラーグラブ
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

        // =========================================================
        // 各PassのCPU実行時間をTracyで計測
        // アロケーションを防ぐため、キャッシュ済みの _tracyZoneName を直接渡す
        // =========================================================
        ZoneScoped;
        ZoneName(pass->_tracyZoneName.c_str(), pass->_tracyZoneName.size());

#if defined(ENABLE_EDITOR)
        if (annotation) {
            int len = MultiByteToWideChar(CP_UTF8, 0, pass->_passName.c_str(), -1, nullptr, 0);
            std::wstring wname(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, pass->_passName.c_str(), -1, &wname[0], len);
            annotation->BeginEvent(wname.c_str());
        }
#endif

        // =========================================================
        // ★修正: マクロを使わず、キャッシュした固有タグでGPU計測を実行する
        // =========================================================
#if defined(TRACY_ENABLE)
        if (auto tracyCtx = Graphics::Instance().GetTracyContext()) {

            // ★ 究極のハック:
            // TracyD3D11Zone マクロの内部実装を直接展開して、
            // 作成済みの pass->_gpuSourceLocation を渡す。
            // 変数名の重複を避けるため、一意なローカル変数を定義する。
            tracy::D3D11ZoneScope __tracy_gpu_zone(tracyCtx, pass->_gpuSourceLocation, true);

            // ※もし上記で「true が多すぎる（引数エラー）」が出た場合は、
            //   tracy::D3D11ZoneScope __tracy_gpu_zone(tracyCtx, pass->_gpuSourceLocation); 
            //   に戻しつつ、以下の #else のブロックを使ってください。

            pass->Execute(ctx);
        }
        else {
            pass->Execute(ctx);
        }
#else
        pass->Execute(ctx);
#endif

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