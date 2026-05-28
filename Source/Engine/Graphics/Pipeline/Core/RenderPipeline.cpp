#include "RenderPipeline.h"

#include <d3d11_1.h> // ID3DUserDefinedAnnotation
#include <wrl/client.h>
#include <Windows.h> // MultiByteToWideChar

void RenderPipeline::Initialize(const RenderServices& services)
{
    _services = services;
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

#if defined(_DEBUG) || defined(PROFILE)
    // GPU マーカー：Pix / RenderDoc 上でパス境界を識別可能にする。
    // 取得失敗時はマーカーなしで続行（リリースモードと同じ動作）。
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    ctx.dc->QueryInterface(IID_PPV_ARGS(annotation.GetAddressOf()));
#endif

    for (auto& pass : _passes) {
        if (!pass->enabled) continue;

#if defined(_DEBUG) || defined(PROFILE)
        if (annotation) {
            wchar_t wname[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, pass->GetName(), -1, wname, 64);
            annotation->BeginEvent(wname);
        }
#endif

        pass->Execute(ctx);

#if defined(_DEBUG) || defined(PROFILE)
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