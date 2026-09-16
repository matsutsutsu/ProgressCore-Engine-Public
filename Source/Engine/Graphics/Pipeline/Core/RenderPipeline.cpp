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
    // 追加と逆順で Shutdown（後から登録したパスが先に依存解放される設計）
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
    // GPU マーカー：Pix / RenderDoc 上でパス境界を識別可能にする。
    // 取得失敗時はマーカーなしで続行（リリースモードと同じ動作）。
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    ctx.dc->QueryInterface(IID_PPV_ARGS(annotation.GetAddressOf()));
#endif

    for (auto &pass : m_passes)
    {
        if (!pass->enabled) continue;

        // =========================================================
        // 各PassのCPU実行時間をTracyで計測
        // アロケーションを防ぐため、キャッシュ済みの m_tracyZoneName を直接渡す
        // =========================================================
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

        // =========================================================
        // マクロを使わず、キャッシュした固有タグでGPU計測を実行する
        // =========================================================
#if defined(TRACY_ENABLE)
        if (auto tracyCtx = m_services.tracyContext)
        {

            // TracyD3D11Zone マクロの内部実装を直接展開して、
            // 作成済みの pass->m_gpuSourceLocation を渡す。
            // 変数名の重複を避けるため、一意なローカル変数を定義する。
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