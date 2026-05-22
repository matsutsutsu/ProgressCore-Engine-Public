#include "MainToolbarWindow.h"
#include "Engine/Platform/Logger.h"
#include <imgui.h>

MainToolbarWindow::MainToolbarWindow() : EditorWindow("Toolbar") {}

void MainToolbarWindow::DrawContents(EditorContext& context)
{
    // =========================================================
    // ツールバーを中央寄せにするための計算
    // =========================================================
    float buttonWidth = 50.0f;
    float buttonHeight = 24.0f;
    // ボタン2つ分 + その間のスペース
    float totalWidth = (buttonWidth * 2) + ImGui::GetStyle().ItemSpacing.x;

    // ウィンドウの中央に配置
    ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x - totalWidth) * 0.5f);

    // =========================================================
    // ★ 停止 (Stop) ボタン
    // =========================================================
    if (context.playState != EditorPlayState::Stopped) {
        if (ImGui::Button("■ Stop", ImVec2(buttonWidth, buttonHeight))) {
            context.playState = EditorPlayState::Stopped;
            // バックアップしておいた状態をロードして「巻き戻す」
            context.pendingLoadScenePath = context.tempBackupScenePath;
            CCL_LOG_INFO(LogCategory::Editor, "Play In Editor: Stopped and Restored.");
        }
    }
    else {
        // 既に止まっている時はボタンを押せない（グレーアウト）状態にする
        ImGui::BeginDisabled();
        ImGui::Button("■ Stop", ImVec2(buttonWidth, buttonHeight));
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    // =========================================================
    // ★ 再生 (Play) / 一時停止 (Pause) ボタン
    // =========================================================
    if (context.playState == EditorPlayState::Stopped) {
        if (ImGui::Button("▶ Play", ImVec2(buttonWidth, buttonHeight))) {
            // 再生開始時に、現在の状態をバックアップ保存する
            context.pendingSaveScenePath = context.tempBackupScenePath;
            context.playState = EditorPlayState::Playing;
            CCL_LOG_INFO(LogCategory::Editor, "Play In Editor: Started.");
        }
    }
    else if (context.playState == EditorPlayState::Playing) {
        if (ImGui::Button("|| Pause", ImVec2(buttonWidth, buttonHeight))) {
            context.playState = EditorPlayState::Paused;
        }
    }
    else if (context.playState == EditorPlayState::Paused) {
        if (ImGui::Button("▶ Resume", ImVec2(buttonWidth, buttonHeight))) {
            context.playState = EditorPlayState::Playing;
        }
    }
}