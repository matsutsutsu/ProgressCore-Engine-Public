#pragma once
// ===========================================================================
// 概要: エディタ・デバッグ用 ImGui の配色プリセット集。
//
// 責務:
//   - ImGui のスタイル（色・角丸・余白）をプリセットごとに設定する
//
// 備考: 使うプリセットは ImGuiRenderer::Initialize で 1 つ選んで呼ぶ。ゲームの UI には影響しない。
// ===========================================================================

namespace ImGuiThemes
{
    void ApplyDark();
    void ApplyDarcula();        // Darcula 風（IntelliJ IDEA）
    void ApplySkeltonDarcula(); // Darcula 風の配色を半透明にしたもの
    void ApplyLight();
    void ApplyClassic();
    void ApplyEditorDark();

    void ApplyUnityDark();        // Unity Editor 風
    void ApplyVisualStudioDark(); // Visual Studio Dark 風

    void ApplyCatppuccinMocha();
    void ApplyTokyoNight();
    void ApplyDracula();
    void ApplyNord();
} // namespace ImGuiThemes
