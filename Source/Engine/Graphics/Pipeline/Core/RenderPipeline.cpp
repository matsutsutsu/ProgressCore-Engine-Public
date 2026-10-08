#include "RenderPipeline.h"

#include <d3d11_1.h> // ID3DUserDefinedAnnotation
#include <wrl/client.h>
#include <Windows.h> // MultiByteToWideChar

#include "tracy/TracyD3D11.hpp"

void RenderPipeline::Initialize(const RenderServices &services)
{
    m_services = services;
}

void RenderPipeline::Shutdown()
{
    // 後から追加したパスほど、前のパスが作ったものに頼っている可能性があるので先に止める
    for (auto it = m_passes.rbegin(); it != m_passes.rend(); ++it)
    {
        (*it)->Shutdown();
    }
    m_passes.clear();
}

void RenderPipeline::Execute(const RenderFrameContext &ctx)
{
    if (!ctx.dc) return;

#if defined(ENABLE_EDITOR)
    // GPU マーカー。PIX / RenderDoc でパスの区切りが分かるようにする。
    // 取れなければマーカー無しで続ける
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    ctx.dc->QueryInterface(IID_PPV_ARGS(annotation.GetAddressOf()));
#endif

    for (auto &pass : m_passes)
    {
        if (!pass->enabled) continue;

        // パスごとの CPU 時間を Tracy で計る。名前は AddPass で作っておいたものを渡す
        ZoneScoped;
        ZoneName(pass->m_tracyZoneName.c_str(), pass->m_tracyZoneName.size());

#if defined(ENABLE_EDITOR)
        if (annotation)
        {
            int          len = MultiByteToWideChar(CP_UTF8, 0, pass->m_passName.c_str(), -1, nullptr, 0);
            std::wstring wname(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, pass->m_passName.c_str(), -1, &wname[0], len);
            annotation->BeginEvent(wname.c_str());
        }
#endif

#if defined(TRACY_ENABLE)
        if (auto tracyCtx = m_services.tracyContext)
        {
            // GPU 時間を計る。TracyD3D11Zone マクロは書いた場所に static な SourceLocationData を
            // 1つ作るので、このループで使うと全パスが同じ名前で計られる。マクロの中身を
            // 展開して、パスごとに AddPass で作った m_gpuSourceLocation を渡す
            tracy::D3D11ZoneScope __tracy_gpu_zone(tracyCtx, pass->m_gpuSourceLocation, true);

            pass->Execute(ctx);
        }
        else
        {
            pass->Execute(ctx);
        }
#else
        pass->Execute(ctx);
#endif

#if defined(ENABLE_EDITOR)
        if (annotation)
        {
            annotation->EndEvent();
        }
#endif
    }
}

void RenderPipeline::OnResize(uint32_t width, uint32_t height)
{
    for (auto &pass : m_passes)
    {
        pass->OnResize(width, height);
    }
}