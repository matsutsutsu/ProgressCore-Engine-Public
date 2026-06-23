/// @file  CameraPathEditorWindow.cpp
/// @brief CameraPathEditorWindowの実装。選択エンティティのパスアセットを抽出し、編集・保存を提供する。

#include "CameraPathEditorWindow.h"
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include <json.hpp>
#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

CameraPathEditorWindow::CameraPathEditorWindow()
    : EditorWindow("Camera Path Editor", false) // デフォルトは非表示
{
}

void CameraPathEditorWindow::DrawContents(EditorContext &context)
{
    if (!context.world)
        return;

    // 1. 対象エンティティのバリデーション
    if (context.selectedEntity == InvalidEntityID)
    {
        ImGui::TextDisabled("No entity selected in Hierarchy.");
        return;
    }

    auto *cart = context.world->GetComponent<CameraDollyCartComponent>(context.selectedEntity);
    if (!cart)
    {
        ImGui::TextDisabled("Selected entity does not have a CameraDollyCartComponent.");
        return;
    }

    // 2. アセットのロード状態チェック
    if (cart->pathAsset.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Warning: pathAsset is empty.");
        ImGui::Text("Please set a Path file in the Inspector first.");
        return;
    }

    if (!cart->loadedPath)
    {
        ImGui::TextDisabled("Path is not loaded yet. Waiting for Logic Update...");
        if (ImGui::Button("Force Create Empty Path Data"))
        {
            cart->loadedPath = std::make_shared<CameraPathAsset>();
        }
        return;
    }

    auto &points       = cart->loadedPath->points;
    bool  pathModified = false;

    // -------------------------------------------------------------
    // UI ヘッダー部 (保存や一括操作)
    // -------------------------------------------------------------
    ImGui::Text("Editing Asset: %s", cart->pathAsset.c_str());
    ImGui::Separator();

    if (ImGui::Button("Save to JSON", ImVec2(120, 30)))
    {
        SavePathToJson(cart->pathAsset, points);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto Save", &_isAutoSave);

    ImGui::Spacing();

    // -------------------------------------------------------------
    // ノード操作 (追加・削除)
    // -------------------------------------------------------------
    if (ImGui::Button("Add Point at End"))
    {
        if (points.empty())
        {
            points.push_back(Vector3(0, 0, 0));
        }
        else
        {
            // 最後の点から少しZ方向にずらして追加
            Vector3 last = points.back();
            points.push_back(last + Vector3(0, 0, 1.0f));
        }
        _selectedPointIndex = static_cast<int>(points.size()) - 1;
        pathModified        = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Insert After Selected") && _selectedPointIndex >= 0 && _selectedPointIndex < points.size())
    {
        Vector3 newPos = points[_selectedPointIndex];
        if (_selectedPointIndex < points.size() - 1)
        {
            // 次の点との中間地点に挿入
            newPos = (points[_selectedPointIndex] + points[_selectedPointIndex + 1]) * 0.5f;
        }
        else
        {
            newPos += Vector3(0, 0, 1.0f);
        }
        points.insert(points.begin() + _selectedPointIndex + 1, newPos);
        _selectedPointIndex++;
        pathModified = true;
    }

    // -------------------------------------------------------------
    // リスト表示部 (ListBox)
    // -------------------------------------------------------------
    ImGui::Text("Waypoints (%zu)", points.size());
    if (ImGui::BeginListBox("##PathList", ImVec2(-FLT_MIN, 150)))
    {
        for (int i = 0; i < points.size(); ++i)
        {
            char label[64];
            snprintf(label, sizeof(label), "[%d] (%.1f, %.1f, %.1f)", i, points[i].x, points[i].y, points[i].z);

            const bool isSelected = (_selectedPointIndex == i);
            if (ImGui::Selectable(label, isSelected))
            {
                _selectedPointIndex = i;
            }

            // アイテムが表示領域に入った時にスクロールを合わせる
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    // -------------------------------------------------------------
    // 選択頂点の編集部 (Inspector風)
    // -------------------------------------------------------------
    if (_selectedPointIndex >= 0 && _selectedPointIndex < points.size())
    {
        ImGui::SeparatorText("Edit Selected Point");

        Vector3 &pt = points[_selectedPointIndex];
        if (ImGui::DragFloat3("Position", &pt.x, 0.1f))
        {
            pathModified = true;
        }

        if (ImGui::Button("Delete Point", ImVec2(120, 0)))
        {
            points.erase(points.begin() + _selectedPointIndex);
            if (_selectedPointIndex >= points.size())
            {
                _selectedPointIndex = static_cast<int>(points.size()) - 1;
            }
            pathModified = true;
        }

        // --- 💡 3Dギズモ(ImGuizmo) との連携用データ注入 ---
        // ここで選択した頂点の座標をギズモで動かせるように、
        // 外部（SceneViewなど）から参照できる形にするのが AAA の定石だ。
        // Context 経由で「現在編集中のパス頂点」のポインタを渡すなどの拡張が考えられる。
    }

    // -------------------------------------------------------------
    // 変更の反映と自動保存
    // -------------------------------------------------------------
    if (pathModified && _isAutoSave)
    {
        SavePathToJson(cart->pathAsset, points);
    }
}

void CameraPathEditorWindow::SavePathToJson(const std::string &relativePath, const std::vector<Vector3> &points)
{
    if (relativePath.empty())
        return;

    nlohmann::json j;
    j["points"] = nlohmann::json::array();

    for (const auto &p : points)
    {
        j["points"].push_back({{"x", p.x},
            {"y", p.y},
            {"z", p.z}});
    }

    // FileSystem経由で書き出し
    FileSystem::WriteAllText(relativePath, j.dump(4));
    CCL_LOG_SUCCESS(LogCategory::Editor, "Saved Camera Path to: %s", relativePath.c_str());
}