//============================================================================
//  DungeonGeneratorWindow.cpp
//  ImGui で「シード入力・2Dプレビュー・3D構築・部屋リスト」を提供（仕様書 §8）
//============================================================================
#include "DungeonGeneratorWindow.h"

#include "Engine/Graphics/RHI/Graphics.h"   // device 取得用

#if ENABLE_EDITOR
#include <imgui.h>
#endif

#include <cstdio>

using namespace RuneForge::Dungeon;

namespace
{
    const char* RoomTypeName(RoomType t)
    {
        switch (t)
        {
        case RoomType::Start:    return "Start";
        case RoomType::Combat:   return "Combat";
        case RoomType::Shop:     return "Shop";
        case RoomType::Treasury: return "Treasury";
        case RoomType::Forge:    return "Forge";
        case RoomType::Elite:    return "Elite";
        case RoomType::Portal:   return "Portal";
        case RoomType::Boss:     return "Boss";
        }
        return "?";
    }
}

DungeonGeneratorWindow::DungeonGeneratorWindow()
    : EditorWindow("Dungeon Generator")
{
}

void DungeonGeneratorWindow::Regenerate()
{
    // 2Dプレビュー用の生成（軽量、決定的）
    _lastResult = DungeonGenerator::Generate(static_cast<uint32_t>(_seed), _floor);
    _hasResult = true;
    DungeonGenerator::DebugPrintGrid(_lastResult);
}

void DungeonGeneratorWindow::BuildInScene(EditorContext& context)
{
    if (!context.world) return;

    ID3D11Device* device = Graphics::Instance().GetDevice();

    // 既存のダンジョンチャンクを消してから再構築（再生成のたびに重複しないように）
    if (_clearBeforeBuild)
        DungeonLevelBuilder::Clear(*context.world);

    // 現在のシード・層で 3D 構築。2Dプレビューと同じシードなので結果は一致する。
    DungeonLevelBuilder::BuildParams params;
    params.seed = static_cast<uint32_t>(_seed);
    params.floorIndex = _floor;
    DungeonLevelBuilder::Build(*context.world, device, params);
}

void DungeonGeneratorWindow::DrawContents(EditorContext& context)
{
#if ENABLE_EDITOR
    // 1. シード入力
    ImGui::InputInt("Seed", &_seed);
    ImGui::SameLine();
    if (ImGui::Button("Random Seed"))
        _seed = static_cast<int>(ImGui::GetTime() * 1000.0) ^ (_seed * 2654435761u);

    // 2. 層番号選択 (1〜5)
    ImGui::SliderInt("Floor", &_floor, 1, 5);

    // 3. 生成（2Dプレビュー）
    if (ImGui::Button("Generate (Preview)", ImVec2(160, 0)))
        Regenerate();

    ImGui::SameLine();
    // 3D シーンに構築（DungeonLevelBuilder 経由）
    if (ImGui::Button("Build in Scene", ImVec2(140, 0)))
    {
        if (!_hasResult) Regenerate();
        BuildInScene(context);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear Scene"))
    {
        if (context.world) DungeonLevelBuilder::Clear(*context.world);
    }

    ImGui::Checkbox("Clear before build", &_clearBeforeBuild);
    ImGui::SameLine();
    if (ImGui::Button("Copy Seed"))
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", _seed);
        ImGui::SetClipboardText(buf);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Zoom", &_zoom, 2.0f, 24.0f, "%.0f px");

    if (!_hasResult)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Press \"Generate (Preview)\" to preview a dungeon.");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Size: %d x %d  (cell=%.1fm)  seed=%u  rooms=%zu",
        _lastResult.width, _lastResult.depth, _lastResult.cellSize,
        _lastResult.usedSeed, _lastResult.rooms.size());
    ImGui::Text("Start=(%d,%d)  Portal=(%d,%d)",
        _lastResult.startCell.i, _lastResult.startCell.j,
        _lastResult.portalCell.i, _lastResult.portalCell.j);

    // 4. グリッド可視化 + 5. 部屋リスト
    DrawGridCanvas();
    ImGui::Separator();
    DrawRoomList();
#else
    (void)context;
#endif
}

void DungeonGeneratorWindow::DrawGridCanvas()
{
#if ENABLE_EDITOR
    const auto& r = _lastResult;
    if (r.width <= 0 || r.depth <= 0) return;

    const float cell = _zoom;
    const ImVec2 canvasSize(r.width * cell, r.depth * cell);

    ImGui::BeginChild("DungeonCanvas", ImVec2(0, 360), true,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const ImU32 colWall = IM_COL32(90, 90, 90, 255);
    const ImU32 colFloor = IM_COL32(15, 15, 18, 255);
    const ImU32 colStart = IM_COL32(60, 200, 80, 255);
    const ImU32 colPortal = IM_COL32(60, 120, 230, 255);
    const ImU32 colBoss = IM_COL32(220, 60, 60, 255);
    const ImU32 colShop = IM_COL32(220, 200, 60, 255);

    // j(Z) が大きいほど上に来るよう画面Yを反転
    auto cellRectMin = [&](int i, int j) -> ImVec2
        {
            float x = origin.x + i * cell;
            float y = origin.y + (r.depth - 1 - j) * cell;
            return ImVec2(x, y);
        };

    for (int i = 0; i < r.width; ++i)
        for (int j = 0; j < r.depth; ++j)
        {
            ImVec2 a = cellRectMin(i, j);
            ImVec2 b(a.x + cell, a.y + cell);
            dl->AddRectFilled(a, b, r.solid[i][j] ? colWall : colFloor);
        }

    for (const auto& room : r.rooms)
        for (const auto& m : room.spawnMarkers)
        {
            ImU32 c = 0;
            if (m.type == SpawnMarkerType::Shop || m.type == SpawnMarkerType::Forge) c = colShop;
            else if (m.type == SpawnMarkerType::Boss) c = colBoss;
            if (c)
            {
                ImVec2 a = cellRectMin(m.cell.i, m.cell.j);
                dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), c);
            }
        }

    {
        ImVec2 a = cellRectMin(r.startCell.i, r.startCell.j);
        dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), colStart);
        ImVec2 p = cellRectMin(r.portalCell.i, r.portalCell.j);
        dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), colPortal);
    }

    ImGui::Dummy(canvasSize);
    ImGui::EndChild();

    ImGui::TextDisabled("Legend:");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.24f, 0.78f, 0.31f, 1), "Start");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.24f, 0.47f, 0.90f, 1), "Portal");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.86f, 0.23f, 0.23f, 1), "Boss");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.86f, 0.78f, 0.24f, 1), "Shop/Forge");
#endif
}

void DungeonGeneratorWindow::DrawRoomList()
{
#if ENABLE_EDITOR
    const auto& r = _lastResult;
    ImGui::Text("Rooms (%zu):", r.rooms.size());

    for (size_t idx = 0; idx < r.rooms.size(); ++idx)
    {
        const RoomMeta& room = r.rooms[idx];
        char header[128];
        std::snprintf(header, sizeof(header), "[%zu] %s  ([%d,%d]-[%d,%d])  doors:%zu##room%zu",
            idx, RoomTypeName(room.type),
            room.minI, room.minJ, room.maxI, room.maxJ,
            room.doors.size(), idx);

        if (ImGui::CollapsingHeader(header))
        {
            ImGui::Indent();
            ImGui::Text("Type: %s", RoomTypeName(room.type));
            ImGui::Text("Cell rect: (%d,%d) - (%d,%d)",
                room.minI, room.minJ, room.maxI, room.maxJ);
            ImGui::Text("Doors: %zu", room.doors.size());
            for (const auto& d : room.doors)
            {
                static const char* dirName[4] = { "+X", "-X", "+Z", "-Z" };
                ImGui::BulletText("door @ (%d,%d) dir=%s",
                    d.cell.i, d.cell.j,
                    (d.direction >= 0 && d.direction < 4) ? dirName[d.direction] : "?");
            }
            ImGui::Text("Spawn markers: %zu", room.spawnMarkers.size());
            ImGui::Unindent();
        }
    }
#endif
}