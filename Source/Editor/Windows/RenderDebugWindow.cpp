/// @file RenderDebugWindow.cpp
/// @brief RenderDebugRegistry の一覧を、種別に応じて可視化して ImGui サムネイル表示する（v2）。
#include "RenderDebugWindow.h"
#include "Engine/Graphics/Renderer/RenderDebugRegistry.h"

#ifdef ENABLE_EDITOR
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <imgui.h>

namespace
{
const char* KindLabel(DebugTexKind k)
{
    switch (k)
    {
        case DebugTexKind::Color:         return "Color";
        case DebugTexKind::HDRColor:      return "HDR";
        case DebugTexKind::Depth:         return "Depth (線形化)";
        case DebugTexKind::SingleChannel: return "R (grayscale)";
        case DebugTexKind::Normal:        return "Normal (remap)";
        case DebugTexKind::Array:         return "Array (slice)";
    }
    return "?";
}
} // namespace
#endif // ENABLE_EDITOR

void RenderDebugWindow::DrawContents(EditorContext& context)
{
#ifdef ENABLE_EDITOR
    const auto& entries = RenderDebugRegistry::Instance().GetEntries();

    // --- 深度線形化用の near/far をカメラから自動取得（無ければ手動値を使う） ---
    if (context.world && context.world->HasResource<Camera*>())
    {
        if (Camera* cam = context.world->GetResource<Camera*>())
        {
            _nearZ = cam->GetNear();
            _farZ  = cam->GetFar();
        }
    }

    // --- 共通コントロール ---
    ImGui::SliderFloat("Thumbnail Width", &_thumbWidth, 128.0f, 640.0f, "%.0f px");
    ImGui::SliderFloat("HDR Exposure", &_exposure, 0.05f, 8.0f, "%.2f");
    const char* channelItems[] = { "RGB", "R", "G", "B", "A" };
    ImGui::Combo("Channel", &_channelSelect, channelItems, IM_ARRAYSIZE(channelItems));
    ImGui::InputInt("Array Slice", &_arraySlice);
    if (_arraySlice < 0) _arraySlice = 0;
    ImGui::TextDisabled("Depth linearize: near=%.2f far=%.1f (camera)", _nearZ, _farZ);
    ImGui::Separator();

    if (entries.empty())
    {
        ImGui::TextDisabled("登録されたデバッグテクスチャがありません。");
        ImGui::TextDisabled("各 RenderPass で RenderDebugRegistry::Register(...) を呼ぶと");
        ImGui::TextDisabled("ここに一覧表示されます。");
        return;
    }

    ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
    _viz.BeginFrame();

    for (const auto& e : entries)
    {
        ImGui::PushID(&e); // アドレスで一意化

        ImGui::Text("%s", e.name ? e.name : "(unnamed)");
        ImGui::SameLine();
        ImGui::TextDisabled("  %ux%u  [%s]", e.width, e.height, KindLabel(e.kind));

        // 種別 → 可視化パラメータ
        RTVisualizeParams p;
        p.mode          = static_cast<int>(e.kind);
        p.exposure      = _exposure;
        p.nearZ         = _nearZ;
        p.farZ          = _farZ;
        p.useArray      = (e.kind == DebugTexKind::Array) ? 1 : 0;
        p.arraySlice    = _arraySlice;
        p.channelSelect = _channelSelect;

        ID3D11ShaderResourceView* shown = _viz.Visualize(dc, e.srv, e.width, e.height, p);

        if (shown)
        {
            const float aspect = (e.width > 0) ? (float)e.height / (float)e.width : 9.0f / 16.0f;
            const ImVec2 size(_thumbWidth, _thumbWidth * aspect);
            ImGui::Image((ImTextureID)shown, size);

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                const ImVec2 big(_thumbWidth * 1.8f, _thumbWidth * 1.8f * aspect);
                ImGui::Image((ImTextureID)shown, big);
                ImGui::EndTooltip();
            }
        }

        ImGui::Separator();
        ImGui::PopID();
    }
#else
    (void)context;
#endif // ENABLE_EDITOR
}
