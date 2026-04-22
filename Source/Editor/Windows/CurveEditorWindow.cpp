#include "CurveEditorWindow.h"
#include "Engine/Platform/Logger.h"
#include <fstream>
#include <imgui.h>

// nlohmann/json を使うための宣言
using json = nlohmann::json;

CurveEditorWindow::CurveEditorWindow() : EditorWindow("Curve Editor") {
    _editingCurve.name = "NewCurve";
    _editingCurve.keys.push_back({ 0.0f, 1.0f });
    _editingCurve.keys.push_back({ 1.0f, 1.0f });
}

void CurveEditorWindow::DrawContents(EditorContext& context) {
    if (ImGui::Button("Load")) {
        LoadCurve(_currentFilePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        SaveCurve(_currentFilePath);
    }

    ImGui::Separator();

    char nameBuf[64];
    strcpy_s(nameBuf, _editingCurve.name.c_str());
    if (ImGui::InputText("Curve Name", nameBuf, sizeof(nameBuf))) {
        _editingCurve.name = nameBuf;
    }

    char pathBuf[256];
    strcpy_s(pathBuf, _currentFilePath.c_str());
    if (ImGui::InputText("Save Path", pathBuf, sizeof(pathBuf))) {
        _currentFilePath = pathBuf;
    }

    ImGui::Separator();

    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 300);

    CurveDelegate delegate(&_editingCurve);

    ImGui::Text("X: Progress (0.0 to 1.0)  /  Y: Speed Multiplier");

    // ★修正: min_view, max_view は Delegate 内部で定義されるようになったため、引数から削除
    ImCurveEdit::Edit(delegate, size, 1);
}

void CurveEditorWindow::SaveCurve(const std::string& path) {
    std::ofstream os(path);
    if (!os.is_open()) {
        CCL_LOG_ERROR(LogCategory::Editor, "Failed to save curve: %s", path.c_str());
        return;
    }
    json j = _editingCurve;
    os << j.dump(4);
    CCL_LOG_SUCCESS(LogCategory::Editor, "Curve saved to %s", path.c_str());
}

void CurveEditorWindow::LoadCurve(const std::string& path) {
    std::ifstream is(path);
    if (!is.is_open()) return;

    try {
        json j;
        is >> j;
        _editingCurve = j.get<AnimationCurve>();
    }
    catch (...) {
        CCL_LOG_ERROR(LogCategory::Editor, "Failed to parse curve JSON.");
    }
}