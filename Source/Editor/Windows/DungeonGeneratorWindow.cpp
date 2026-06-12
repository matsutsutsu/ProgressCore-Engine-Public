//============================================================================
//  DungeonGeneratorWindow.cpp
//  ImGui で「シード入力・生成・グリッド可視化・部屋リスト」を提供（仕様書 §8）
//============================================================================
#include "DungeonGeneratorWindow.h"

#include "Engine/Serialization/Factory/SceneInitializer.h" // SpawnDungeonTest を呼ぶため

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
    _lastResult = DungeonGenerator::Generate(static_cast<uint32_t>(_seed), _floor);
    _hasResult = true;
    // コンソールにも ASCII を出しておく（デバッグ用シード確認）
    DungeonGenerator::DebugPrintGrid(_lastResult);
}

void DungeonGeneratorWindow::SpawnToScene(EditorContext& context)
{
    if (!context.world) return;
    // 現在のシード・層でシーンに 3D 生成する。
    // （SpawnDungeonTest 内部でも Generate を呼ぶので決定的に同じ結果になる）
    SceneInitializer::SpawnDungeonTest(
        *context.world, nullptr,
        static_cast<uint32_t>(_seed), _floor);
}

void DungeonGeneratorWindow::DrawContents(EditorContext& context)
{
#if ENABLE_EDITOR
    // ---------------------------------------------------------------
    // 1. シード入力
    // ---------------------------------------------------------------
    ImGui::InputInt("Seed", &_seed);
    ImGui::SameLine();
    if (ImGui::Button("Random Seed"))
    {
        // ImGui のフレームカウンタ等から雑にシードを振る（決定性は Generate 側で担保）
        _seed = static_cast<int>(ImGui::GetTime() * 1000.0) ^ (_seed * 2654435761u);
    }

    // ---------------------------------------------------------------
    // 2. 層番号選択 (1〜5)
    // ---------------------------------------------------------------
    ImGui::SliderInt("Floor", &_floor, 1, 5);

    // ---------------------------------------------------------------
    // 3. 生成ボタン
    // ---------------------------------------------------------------
    if (ImGui::Button("Generate", ImVec2(120, 0)))
    {
        Regenerate();
    }
    ImGui::SameLine();
    // 3D シーンに建てる（要 Generate 済み or 押下時に内部で生成）
    if (ImGui::Button("Spawn to Scene", ImVec2(140, 0)))
    {
        if (!_hasResult) Regenerate();
        SpawnToScene(context);
    }
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
        ImGui::TextDisabled("Press \"Generate\" to build a dungeon.");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Size: %d x %d  (cell=%.1fm)  seed=%u  rooms=%zu",
        _lastResult.width, _lastResult.depth, _lastResult.cellSize,
        _lastResult.usedSeed, _lastResult.rooms.size());
    ImGui::Text("Start=(%d,%d)  Portal=(%d,%d)",
        _lastResult.startCell.i, _lastResult.startCell.j,
        _lastResult.portalCell.i, _lastResult.portalCell.j);

    // ---------------------------------------------------------------
    // 4. グリッド可視化 + 5. 部屋リスト を左右分割
    // ---------------------------------------------------------------
    DrawGridCanvas();
    ImGui::Separator();
    DrawRoomList();
#else
    (void)0;
#endif
}

void DungeonGeneratorWindow::DrawGridCanvas()
{
#if ENABLE_EDITOR
    const auto& r = _lastResult;
    if (r.width <= 0 || r.depth <= 0) return;

    const float cell = _zoom;
    const ImVec2 canvasSize(r.width * cell, r.depth * cell);

    // スクロール可能な子ウィンドウ内に描画
    ImGui::BeginChild("DungeonCanvas", ImVec2(0, 360), true,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // 色（仕様書 §8）
    const ImU32 colWall = IM_COL32(90, 90, 90, 255); // 壁=グレー
    const ImU32 colFloor = IM_COL32(15, 15, 18, 255); // 床=黒
    const ImU32 colStart = IM_COL32(60, 200, 80, 255); // スタート=緑
    const ImU32 colPortal = IM_COL32(60, 120, 230, 255); // ポータル=青
    const ImU32 colBoss = IM_COL32(220, 60, 60, 255); // ボス=赤
    const ImU32 colShop = IM_COL32(220, 200, 60, 255); // ショップ=黄

    // j（Z）が大きいほど上に来るよう、画面 Y は反転して描く。
    auto cellRectMin = [&](int i, int j) -> ImVec2
        {
            float x = origin.x + i * cell;
            float y = origin.y + (r.depth - 1 - j) * cell;
            return ImVec2(x, y);
        };

    // 床/壁を一括描画
    for (int i = 0; i < r.width; ++i)
        for (int j = 0; j < r.depth; ++j)
        {
            ImVec2 a = cellRectMin(i, j);
            ImVec2 b(a.x + cell, a.y + cell);
            dl->AddRectFilled(a, b, r.solid[i][j] ? colWall : colFloor);
        }

    // 部屋のスポーンマーカー（ショップ/ボス）を色付け
    for (const auto& room : r.rooms)
    {
        for (const auto& m : room.spawnMarkers)
        {
            ImU32 c = 0;
            if (m.type == SpawnMarkerType::Shop || m.type == SpawnMarkerType::Forge) c = colShop;
            else if (m.type == SpawnMarkerType::Boss) c = colBoss;
            if (c)
            {
                ImVec2 a = cellRectMin(m.cell.i, m.cell.j);
                ImVec2 b(a.x + cell, a.y + cell);
                dl->AddRectFilled(a, b, c);
            }
        }
    }

    // スタート（緑）・ポータル（青）
    {
        ImVec2 a = cellRectMin(r.startCell.i, r.startCell.j);
        dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), colStart);
        ImVec2 p = cellRectMin(r.portalCell.i, r.portalCell.j);
        dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), colPortal);
    }

    // 子ウィンドウのスクロール領域を確保するためダミーを置く
    ImGui::Dummy(canvasSize);
    ImGui::EndChild();

    // 凡例
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