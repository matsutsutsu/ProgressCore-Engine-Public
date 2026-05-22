#include "GameViewWindow.h"


void GameViewWindow::DrawContents(EditorContext& context)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // 16:9 のアスペクト比を維持してピッタリ収まるサイズを計算
    const float targetAspect = 16.0f / 9.0f;
    float windowAspect = avail.x / avail.y;
    ImVec2 imageSize = (windowAspect > targetAspect)
        ? ImVec2(avail.y * targetAspect, avail.y)
        : ImVec2(avail.x, avail.x / targetAspect);

    // ウィンドウ内で中央に配置するための計算
    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(
        cursorPos.x + (avail.x - imageSize.x) * 0.5f,
        cursorPos.y + (avail.y - imageSize.y) * 0.5f
    ));

    // ★ テクスチャが渡されていれば描画する
    if (context.gameSRV) {
        ImGui::Image(context.gameSRV, imageSize);
    }
    else {
        ImGui::Text("No Render Target Available.");
    }
}
