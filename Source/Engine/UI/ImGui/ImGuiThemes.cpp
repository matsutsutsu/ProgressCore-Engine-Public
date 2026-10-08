#include "ImGuiThemes.h"
#ifdef ENABLE_EDITOR

#include <imgui.h>

void ImGuiThemes::ApplyLight()
{
    ImGui::StyleColorsLight();
}

void ImGuiThemes::ApplyDark()
{
    ImGui::StyleColorsDark();
}

void ImGuiThemes::ApplyClassic()
{
    ImGui::StyleColorsClassic();
}

void ImGuiThemes::ApplyEditorDark()
{
    ImGuiStyle &style  = ImGui::GetStyle();
    ImVec4     *colors = style.Colors;

    // 背景（ほぼ黒）
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    // ヘッダー（コンポーネントバー）は少し青みのある暗いグレー
    colors[ImGuiCol_Header]        = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]  = ImVec4(0.30f, 0.35f, 0.40f, 1.00f);

    // 入力枠は背景よりわずかに明るくして区別できるようにする
    colors[ImGuiCol_FrameBg]        = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    // タイトルバー
    colors[ImGuiCol_TitleBg]       = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    // 枠線とセパレータは背景より暗くして影のように見せる
    colors[ImGuiCol_Border]    = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);

    // ボタン
    colors[ImGuiCol_Button]        = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonActive]  = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

    style.FrameRounding    = 2.0f; // 入力枠の角を少しだけ丸める
    style.WindowRounding   = 0.0f;
    style.FrameBorderSize  = 0.0f; // 入力枠の線を消す
    style.WindowBorderSize = 1.0f;
}

void ImGuiThemes::ApplyDarcula()
{
    ImGuiStyle &style  = ImGui::GetStyle();
    ImVec4     *colors = style.Colors;

    colors[ImGuiCol_WindowBg]       = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.23f, 0.23f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.33f, 0.33f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]        = ImVec4(0.13f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
}

void ImGuiThemes::ApplySkeltonDarcula()
{
    ImGuiStyle &style        = ImGui::GetStyle();
    ImVec4     *colors       = style.Colors;
    float       SkeltonAlpha = 0.75f;

    style.Alpha = SkeltonAlpha;

    colors[ImGuiCol_WindowBg]       = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.23f, 0.23f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.33f, 0.33f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]        = ImVec4(0.13f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
}

void ImGuiThemes::ApplyUnityDark()
{
    ImGuiStyle &style  = ImGui::GetStyle();
    ImVec4     *colors = style.Colors;

    colors[ImGuiCol_WindowBg]       = ImVec4(0.176f, 0.176f, 0.176f, 1.000f);
    colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_Button]         = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);
    colors[ImGuiCol_TitleBg]        = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
}

void ImGuiThemes::ApplyVisualStudioDark()
{
    ImGuiStyle &style  = ImGui::GetStyle();
    ImVec4     *colors = style.Colors;

    colors[ImGuiCol_WindowBg]       = ImVec4(0.105f, 0.105f, 0.105f, 1.0f);
    colors[ImGuiCol_Header]         = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    colors[ImGuiCol_Button]         = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
    colors[ImGuiCol_TitleBg]        = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.13f, 0.13f, 0.13f, 1.0f);
}

void ImGuiThemes::ApplyCatppuccinMocha()
{
    ImVec4 *colors                 = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg]      = ImVec4(0.18f, 0.17f, 0.25f, 1.00f);
    colors[ImGuiCol_Header]        = ImVec4(0.27f, 0.26f, 0.36f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.34f, 0.46f, 1.00f);
    colors[ImGuiCol_FrameBg]       = ImVec4(0.20f, 0.19f, 0.28f, 1.00f);
    colors[ImGuiCol_Button]        = ImVec4(0.35f, 0.34f, 0.46f, 1.00f);
    colors[ImGuiCol_Text]          = ImVec4(0.83f, 0.83f, 0.90f, 1.00f);
    colors[ImGuiCol_Border]        = ImVec4(0.20f, 0.19f, 0.28f, 1.00f);
}

void ImGuiThemes::ApplyTokyoNight()
{
    ImVec4 *colors            = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_Header]   = ImVec4(0.17f, 0.17f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBg]  = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_Button]   = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_Text]     = ImVec4(0.70f, 0.70f, 0.80f, 1.00f);
    colors[ImGuiCol_Border]   = ImVec4(0.12f, 0.12f, 0.20f, 1.00f);
}

void ImGuiThemes::ApplyDracula()
{
    ImVec4 *colors            = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f); // Dracula Background
    colors[ImGuiCol_Header]   = ImVec4(0.27f, 0.27f, 0.35f, 1.00f); // Current Line
    colors[ImGuiCol_FrameBg]  = ImVec4(0.25f, 0.25f, 0.32f, 1.00f); // Selection
    colors[ImGuiCol_Button]   = ImVec4(0.37f, 0.27f, 0.50f, 1.00f); // Purple
    colors[ImGuiCol_Text]     = ImVec4(0.97f, 0.97f, 0.97f, 1.00f); // Foreground
}

void ImGuiThemes::ApplyNord()
{
    ImGuiStyle &style  = ImGui::GetStyle();
    ImVec4     *colors = style.Colors;

    // 背景・枠
    colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.20f, 0.23f, 1.00f); // Nord 0
    colors[ImGuiCol_FrameBg]  = ImVec4(0.24f, 0.27f, 0.32f, 1.00f); // Nord 2
    colors[ImGuiCol_Border]   = ImVec4(0.27f, 0.30f, 0.35f, 1.00f); // Nord 1

    // タブ。非アクティブなタブは背景に近い色にして、画面全体が青くなりすぎないようにする
    colors[ImGuiCol_Tab]        = ImVec4(0.22f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_TabActive]  = ImVec4(0.53f, 0.75f, 0.82f, 1.00f); // Nord 8
    colors[ImGuiCol_TabHovered] = ImVec4(0.37f, 0.44f, 0.52f, 1.00f);

    // フォーカスの無いウィンドウのタブ
    colors[ImGuiCol_TabUnfocused]       = ImVec4(0.18f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.27f, 0.30f, 0.35f, 1.00f);

    // ヘッダー・ボタン
    colors[ImGuiCol_Header]        = ImVec4(0.27f, 0.30f, 0.35f, 1.00f); // Nord 1
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.34f, 0.40f, 0.48f, 1.00f); // Nord 3
    colors[ImGuiCol_Button]        = ImVec4(0.34f, 0.40f, 0.48f, 1.00f); // Nord 3

    // テキスト
    colors[ImGuiCol_Text] = ImVec4(0.85f, 0.88f, 0.91f, 1.00f); // Nord 4

    // 余白
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding  = ImVec2(4, 3);
    style.ItemSpacing   = ImVec2(8, 4);

    // 角を丸めない
    style.WindowRounding = 0.0f;
    style.FrameRounding  = 0.0f;
    style.GrabRounding   = 0.0f;

    // 枠線はウィンドウの外枠だけ残す
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;

    style.ScrollbarSize       = 12.0f;
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f); // 左寄せ・縦中央
}

#endif