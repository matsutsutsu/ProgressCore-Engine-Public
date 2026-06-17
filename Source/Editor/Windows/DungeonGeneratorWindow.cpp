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
    _lastResult = DungeonGenerator::Generate(static_cast<uint32_t>(_seed), _floor, _gen);
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
    params.gen = _gen; // ★ デバッグ調整したパラメータを反映
    DungeonLevelBuilder::Build(*context.world, device, params);
}

void DungeonGeneratorWindow::DrawContents(EditorContext& context)
{
#if ENABLE_EDITOR
    // モード切り替え（自動生成 / 手作り）
    if (ImGui::RadioButton("Procedural", _mode == Mode::Procedural)) _mode = Mode::Procedural;
    ImGui::SameLine();
    if (ImGui::RadioButton("Hand-Made (Debug)", _mode == Mode::HandMade)) _mode = Mode::HandMade;
    ImGui::Separator();

    if (_mode == Mode::HandMade)
    {
        DrawHandMadeControls(context);
        return;
    }

    // ===== 以下、自動生成モード =====
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

    // ★ 生成パラメータ調整（デバッグ）
    if (ImGui::CollapsingHeader("Generation Params"))
    {
        ImGui::Indent();
        ImGui::SliderInt("Min Rooms", &_gen.minRooms, 2, 30);
        ImGui::SliderInt("Max Rooms", &_gen.maxRooms, _gen.minRooms, 40);
        ImGui::SliderInt("Rooms/Floor Bonus", &_gen.roomsPerFloorBonus, 0, 5);
        ImGui::SliderFloat("Branch Chance", &_gen.branchChance, 0.0f, 1.0f, "%.2f");
        ImGui::SliderInt("Max Branch/Room", &_gen.maxBranchPerRoom, 1, 4);
        ImGui::SliderFloat("Regrow Chance", &_gen.regrowChance, 0.0f, 1.0f, "%.2f");
        ImGui::Separator();
        ImGui::SliderInt("Corridor Width", &_gen.corridorWidth, 1, 5);
        ImGui::SliderInt("Cell Tiles W", &_gen.cellTilesW, 5, 25);
        ImGui::SliderInt("Cell Tiles H", &_gen.cellTilesH, 5, 25);
        ImGui::SliderInt("Cell Gap", &_gen.cellGap, 1, 8);
        ImGui::Checkbox("Place Special Rooms", &_gen.placeSpecialRooms);
        ImGui::TextDisabled("Branch 0=linear, 0.35=balanced, 0.6+=maze");
        ImGui::Unindent();
    }

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

//============================================================================
//  手作りマップ（デバッグ）モード
//============================================================================
void DungeonGeneratorWindow::BuildLayoutInScene(EditorContext& context)
{
#if ENABLE_EDITOR
    if (!context.world) return;
    ID3D11Device* device = Graphics::Instance().GetDevice();

    if (_clearBeforeBuild)
        DungeonLevelBuilder::Clear(*context.world);

    // 手作りレイアウト → DungeonGenerateResult → 共通パイプライン
    DungeonGenerateResult result = _layout.ToResult(/*tagSeed*/ 1);

    DungeonLevelBuilder::BuildParams params;
    // 手作りマップ用に Prefab スポーンはマーカー任せ（敵/宝箱など）
    params.enemyPrefab = "Assets/Prefabs/Enemy/Slime.json";
    params.chestPrefab = "Assets/Prefabs/Item/Chest.json";
    params.portalPrefab = "Assets/Prefabs/Portal/ExitPortal.json";
    DungeonLevelBuilder::BuildFromResult(*context.world, device, result, params);
#endif
}

void DungeonGeneratorWindow::DrawHandMadeControls(EditorContext& context)
{
#if ENABLE_EDITOR
    // --- サイズ変更 / 新規 ---
    ImGui::SetNextItemWidth(80); ImGui::InputInt("W", &_resizeW); ImGui::SameLine();
    ImGui::SetNextItemWidth(80); ImGui::InputInt("H", &_resizeH); ImGui::SameLine();
    if (ImGui::Button("New / Resize"))
    {
        _resizeW = (_resizeW < 3) ? 3 : _resizeW;
        _resizeH = (_resizeH < 3) ? 3 : _resizeH;
        _layout.Reset(_resizeW, _resizeH);
    }

    // --- 保存 / 読み込み（FileSystem 経由）---
    ImGui::SetNextItemWidth(280);
    ImGui::InputText("Path", _savePath, sizeof(_savePath));
    if (ImGui::Button("Save"))  _layout.SaveToJson(_savePath);
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        if (_layout.LoadFromJson(_savePath)) { _resizeW = _layout.width; _resizeH = _layout.depth; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Build in Scene"))
    {
        BuildLayoutInScene(context);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Scene"))
    {
        if (context.world) DungeonLevelBuilder::Clear(*context.world);
    }
    ImGui::Checkbox("Clear before build", &_clearBeforeBuild);

    // --- 編集ツール選択 ---
    ImGui::Separator();
    ImGui::Text("Edit Tool:");
    ImGui::RadioButton("Wall/Floor", &_editTool, 0); ImGui::SameLine();
    ImGui::RadioButton("Start", &_editTool, 1); ImGui::SameLine();
    ImGui::RadioButton("Portal", &_editTool, 2); ImGui::SameLine();
    ImGui::RadioButton("Enemy", &_editTool, 3); ImGui::SameLine();
    ImGui::RadioButton("Chest", &_editTool, 4);

    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Zoom", &_zoom, 6.0f, 32.0f, "%.0f px");

    ImGui::Separator();
    DrawLayoutCanvas();
#endif
}

void DungeonGeneratorWindow::DrawLayoutCanvas()
{
#if ENABLE_EDITOR
    auto& L = _layout;
    if (L.width <= 0 || L.depth <= 0) return;

    const float cell = _zoom;
    const ImVec2 canvasSize(L.width * cell, L.depth * cell);

    ImGui::BeginChild("LayoutCanvas", ImVec2(0, 420), true,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const ImU32 colWall = IM_COL32(90, 90, 90, 255);
    const ImU32 colFloor = IM_COL32(25, 25, 30, 255);
    const ImU32 colGridLn = IM_COL32(50, 50, 55, 255);
    const ImU32 colStart = IM_COL32(60, 200, 80, 255);
    const ImU32 colPortal = IM_COL32(60, 120, 230, 255);
    const ImU32 colEnemy = IM_COL32(220, 80, 80, 255);
    const ImU32 colChest = IM_COL32(220, 200, 60, 255);

    auto cellMin = [&](int i, int j) -> ImVec2 {
        return ImVec2(origin.x + i * cell, origin.y + (L.depth - 1 - j) * cell);
        };

    // セル描画
    for (int i = 0; i < L.width; ++i)
        for (int j = 0; j < L.depth; ++j)
        {
            ImVec2 a = cellMin(i, j);
            ImVec2 b(a.x + cell, a.y + cell);
            dl->AddRectFilled(a, b, L.solid[i][j] ? colWall : colFloor);
            dl->AddRect(a, b, colGridLn);
        }

    // マーカー描画
    for (const auto& m : L.markers)
    {
        ImU32 c = (m.type == SpawnMarkerType::Enemy) ? colEnemy
            : (m.type == SpawnMarkerType::Chest) ? colChest : 0;
        if (c)
        {
            ImVec2 a = cellMin(m.cell.i, m.cell.j);
            dl->AddRectFilled(ImVec2(a.x + cell * 0.25f, a.y + cell * 0.25f),
                ImVec2(a.x + cell * 0.75f, a.y + cell * 0.75f), c);
        }
    }
    // スタート / ポータル
    {
        ImVec2 s = cellMin(L.startCell.i, L.startCell.j);
        dl->AddRectFilled(s, ImVec2(s.x + cell, s.y + cell), colStart);
        ImVec2 p = cellMin(L.portalCell.i, L.portalCell.j);
        dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), colPortal);
    }

    // クリック編集（キャンバス上のセルを判定）
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(0))
    {
        ImVec2 mp = ImGui::GetMousePos();
        int ci = (int)((mp.x - origin.x) / cell);
        int rj = (int)((mp.y - origin.y) / cell);
        int cj = (L.depth - 1) - rj; // Y反転
        if (L.InBounds(ci, cj))
        {
            switch (_editTool)
            {
            case 0: // 壁/床トグル（押している間に塗れるよう、クリック時のみ反転）
                if (ImGui::IsMouseClicked(0))
                    L.solid[ci][cj] = !L.solid[ci][cj];
                break;
            case 1: L.startCell = { ci, cj }; L.solid[ci][cj] = false; break;
            case 2: L.portalCell = { ci, cj }; L.solid[ci][cj] = false; break;
            case 3:
                if (ImGui::IsMouseClicked(0)) {
                    L.solid[ci][cj] = false;
                    L.markers.push_back({ { ci, cj }, SpawnMarkerType::Enemy });
                }
                break;
            case 4:
                if (ImGui::IsMouseClicked(0)) {
                    L.solid[ci][cj] = false;
                    L.markers.push_back({ { ci, cj }, SpawnMarkerType::Chest });
                }
                break;
            }
        }
    }

    ImGui::Dummy(canvasSize);
    ImGui::EndChild();

    ImGui::TextDisabled("Left-drag to paint walls. Start=green Portal=blue Enemy=red Chest=yellow");
    ImGui::Text("Size: %d x %d  markers=%zu  Start=(%d,%d) Portal=(%d,%d)",
        L.width, L.depth, L.markers.size(),
        L.startCell.i, L.startCell.j, L.portalCell.i, L.portalCell.j);
    if (ImGui::Button("Clear Markers")) L.markers.clear();
#endif
}