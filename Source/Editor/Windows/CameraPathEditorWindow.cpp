#include "CameraPathEditorWindow.h"
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Camera/Systems/CameraDollySystem.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include <json.hpp>
#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

CameraPathEditorWindow::CameraPathEditorWindow()
    : EditorWindow("Camera Path Editor", false) // 既定は非表示
{
}

void CameraPathEditorWindow::DrawContents(EditorContext &context)
{
    if (!context.world)
        return;

    // 別のエンティティを選択したら、制御点の選択を解除する
    static CCL::ECS::EntityID lastSelectedEntity = CCL::ECS::InvalidEntityID;
    if (context.selectedEntity != lastSelectedEntity)
    {
        m_selectedPointIndex         = -1;
        context.editingPathNodeIndex = -1;
        lastSelectedEntity           = context.selectedEntity;
    }

    // 対象のエンティティと、台車・軌道アセットがそろっているか
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

    auto &asset        = *cart->loadedPath;
    auto &points       = asset.points;
    bool  pathModified = false;

    // ---- 保存とループ設定 ----
    ImGui::Text("Editing Asset: %s", cart->pathAsset.c_str());
    ImGui::Separator();

    if (ImGui::Button("Save to JSON", ImVec2(120, 30)))
    {
        SavePathToJson(cart->pathAsset, asset);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto Save", &m_isAutoSave);

    // ループ設定。CameraDollySystem がこれを見て、終端で止めるか巻き戻すかを決める。
    if (ImGui::Checkbox("Loop (closed path)", &asset.isLoop))
    {
        pathModified = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Length: %.2fm", asset.totalLength);

    ImGui::Spacing();

    // ---- 制御点の追加・挿入 ----
    if (ImGui::Button("Add Point at End"))
    {
        if (points.empty())
        {
            points.push_back(Vector3(0, 0, 0));
        }
        else
        {
            // 最後の点から Z 方向に 1m ずらして追加する
            Vector3 last = points.back();
            points.push_back(last + Vector3(0, 0, 1.0f));
        }
        m_selectedPointIndex = static_cast<int>(points.size()) - 1;
        pathModified         = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Insert After Selected") && m_selectedPointIndex >= 0 && m_selectedPointIndex < points.size())
    {
        Vector3 newPos = points[m_selectedPointIndex];
        if (m_selectedPointIndex < points.size() - 1)
        {
            // 次の点との中間地点に挿入
            newPos = (points[m_selectedPointIndex] + points[m_selectedPointIndex + 1]) * 0.5f;
        }
        else
        {
            newPos += Vector3(0, 0, 1.0f);
        }
        points.insert(points.begin() + m_selectedPointIndex + 1, newPos);
        m_selectedPointIndex++;
        pathModified = true;
    }

    // ---- 制御点の一覧 ----
    ImGui::Text("Waypoints (%zu)", points.size());
    if (ImGui::BeginListBox("##PathList", ImVec2(-FLT_MIN, 150)))
    {
        for (int i = 0; i < points.size(); ++i)
        {
            char label[64];
            snprintf(label, sizeof(label), "[%d] (%.1f, %.1f, %.1f)", i, points[i].x, points[i].y, points[i].z);

            const bool isSelected = (m_selectedPointIndex == i);
            if (ImGui::Selectable(label, isSelected))
            {
                m_selectedPointIndex = i;
            }

            // 一覧を開いたとき、選択中の項目にフォーカスを合わせる
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    // GameViewWindow のギズモが動かす制御点を伝える
    context.editingPathNodeIndex = m_selectedPointIndex;

    // ---- 選択中の制御点の編集 ----
    if (m_selectedPointIndex >= 0 && m_selectedPointIndex < points.size())
    {
        ImGui::SeparatorText("Edit Selected Point");

        Vector3 &pt = points[m_selectedPointIndex];
        if (ImGui::DragFloat3("Position", &pt.x, 0.1f))
        {
            pathModified = true;
        }

        if (ImGui::Button("Delete Point", ImVec2(120, 0)))
        {
            points.erase(points.begin() + m_selectedPointIndex);
            if (m_selectedPointIndex >= points.size())
            {
                m_selectedPointIndex = static_cast<int>(points.size()) - 1;
            }
            pathModified = true;
        }
    }

    // ---- 変更の反映と自動保存（ギズモで動かしたときの反映は GameViewWindow が行う） ----
    if (pathModified)
    {
        // 制御点や isLoop を触ったら LUT を作り直す。
        // これが無いと totalLength と距離→t の対応表が古いままになり、
        // 編集後の軌道上で台車の速度と位置がずれる（再ロードするまで直らない）。
        CameraDollySystem::BuildLookupTable(asset);

        // 進行度が新しい全長を超えていたら丸めておく
        if (asset.totalLength > 0.0f && cart->currentDistance > asset.totalLength)
        {
            cart->currentDistance = asset.isLoop
                                        ? CameraDollySystem::WrapDistance(cart->currentDistance, asset.totalLength)
                                        : asset.totalLength;
        }

        if (m_isAutoSave)
        {
            SavePathToJson(cart->pathAsset, asset);
        }
    }
}

void CameraPathEditorWindow::SavePathToJson(const std::string &relativePath, const CameraPathAsset &asset)
{
    if (relativePath.empty())
        return;

    nlohmann::json j;
    j["points"] = nlohmann::json::array();

    for (const auto &p : asset.points)
    {
        j["points"].push_back({{"x", p.x},
                               {"y", p.y},
                               {"z", p.z}});
    }

    // ループ設定も書き出す（CameraDollySystem がロード時に読む）
    j["isLoop"] = asset.isLoop;

    FileSystem::WriteAllText(relativePath, j.dump(4));
    CCL_LOG_SUCCESS(LogCategory::Editor, "Saved Camera Path to: %s", relativePath.c_str());
}
