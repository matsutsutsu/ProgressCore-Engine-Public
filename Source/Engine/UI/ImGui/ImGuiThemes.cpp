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

    // --- メインカラーパレット ---
    // 暗い背景 (ほぼ黒)
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    // ヘッダー (コンポーネントバー) を少し青みがかった深いグレーにする
    colors[ImGuiCol_Header]        = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]  = ImVec4(0.30f, 0.35f, 0.40f, 1.00f);

    // プロパティの入力枠 (背景よりわずかに明るくして「浮き出させる」)
    colors[ImGuiCol_FrameBg]        = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

    // タイトルバー (ウィンドウのヘッダー)
    colors[ImGuiCol_TitleBg]       = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    // 枠線とセパレータ (非常に暗くして「影」として使う)
    colors[ImGuiCol_Border]    = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);

    // ボタン系
    colors[ImGuiCol_Button]        = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonActive]  = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

    // スタイル調整
    style.FrameRounding    = 2.0f; // 少しだけ角を丸くする (モダンな印象)
    style.WindowRounding   = 0.0f; // ウィンドウは角ばらせる (プロツール感)
    style.FrameBorderSize  = 0.0f; // 枠線を消してフラットにする
    style.WindowBorderSize = 1.0f;
}

// Darcula 風 (IntelliJ IDEA 系)
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

// Unity Editor 風
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

// Visual Studio Dark 風
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

    // --- 背景・基本 ---
    colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.20f, 0.23f, 1.00f); // Nord 0
    colors[ImGuiCol_FrameBg]  = ImVec4(0.24f, 0.27f, 0.32f, 1.00f); // Nord 2
    colors[ImGuiCol_Border]   = ImVec4(0.27f, 0.30f, 0.35f, 1.00f); // Nord 1

    // --- タブ設定 (ここをNord色に変える) ---
    // 非アクティブなタブを背景に溶け込ませる（これが青すぎるのを防ぐコツ）
    colors[ImGuiCol_Tab] = ImVec4(0.22f, 0.25f, 0.29f, 1.00f); // Nord 2に近い色
    // アクティブなタブ (Nordの象徴的な青: Nord 8)
    colors[ImGuiCol_TabActive] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f); // Nord 8 (Ice Blue)
    // ホバー時のタブ
    colors[ImGuiCol_TabHovered] = ImVec4(0.37f, 0.44f, 0.52f, 1.00f); // Nord 4 (Snow Storm)

    // 非アクティブだけど、選択されているウィンドウのタブ
    colors[ImGuiCol_TabUnfocused]       = ImVec4(0.18f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.27f, 0.30f, 0.35f, 1.00f);

    // --- ヘッダー・ボタン ---
    colors[ImGuiCol_Header]        = ImVec4(0.27f, 0.30f, 0.35f, 1.00f); // Nord 1
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.34f, 0.40f, 0.48f, 1.00f); // Nord 3
    colors[ImGuiCol_Button]        = ImVec4(0.34f, 0.40f, 0.48f, 1.00f); // Nord 3

    // --- テキスト ---
    colors[ImGuiCol_Text] = ImVec4(0.85f, 0.88f, 0.91f, 1.00f); // Nord 6

    // =================================================================
    // 2. スタイル設定の直書き (ここを触るとかっこよさが変わる)
    // =================================================================

    // 余白設定
    style.WindowPadding = ImVec2(6, 6); // 少しだけ余裕を持たせる
    style.FramePadding  = ImVec2(4, 3);
    style.ItemSpacing   = ImVec2(8, 4);

    // 角の丸み (0.0f にすると非常にシャープになります)
    style.WindowRounding = 0.0f;
    style.FrameRounding  = 0.0f;
    style.GrabRounding   = 0.0f;

    // 枠線の太さ
    style.WindowBorderSize = 1.0f; // 外枠だけ残す
    style.FrameBorderSize  = 0.0f; // 入力枠の線は消すとモダン

    // その他
    style.ScrollbarSize       = 12.0f;
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f); // テキストを左寄せに
}

#endif