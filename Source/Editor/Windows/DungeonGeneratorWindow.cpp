//============================================================================
//  DungeonGeneratorWindow.cpp
//============================================================================
#include "DungeonGeneratorWindow.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Dialog.h" // プロジェクトのファイルダイアログ
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Serialization/Factory/SceneInitializer.h"

#include <imgui.h>


#include <cstdio>
#include <algorithm>
#include <string>

using namespace RuneForge::Dungeon;

DungeonGeneratorWindow::DungeonGeneratorWindow()
    : EditorWindow("Dungeon Architect Tool")
{
}

void DungeonGeneratorWindow::RegenerateProcedural()
{
    _lastResult = DungeonGenerator::Generate(static_cast<uint32_t>(_seed), _floor, _gen);
    _hasResult = true;
}

void DungeonGeneratorWindow::DrawContents(EditorContext& context)
{
    // ツール全体の共通設定項目
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "Global View Settings");
    ImGui::SliderFloat("Grid Zoom", &_zoom, 4.0f, 32.0f, "%.1fpf");
    ImGui::Checkbox("Clear Chunks Before 3D Build", &_clearBeforeBuild);

    ImGui::Spacing(); // ★修正: Spacer() -> Spacing()
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("DungeonModeTabBar"))
    {
        if (ImGui::BeginTabItem("Procedural (Auto Gen)"))
        {
            DrawProceduralTab(context); // ★ contextを渡す
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Hand-Made (Level Editor)"))
        {
            DrawHandMadeTab(context); // ★ contextを渡す
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// =========================================================================
//  自動生成（Procedural）タブ
// =========================================================================
void DungeonGeneratorWindow::DrawProceduralTab(EditorContext& context)
{
    ImGui::Spacing();
    ImGui::Text("Generation Seeds:");
    ImGui::InputInt("Seed Value", &_seed);
    ImGui::SliderInt("Floor Index", &_floor, 1, 5);

    if (ImGui::Button("Preview 2D Map", ImVec2(-1, 30))) {
        RegenerateProcedural();
    }

    if (_hasResult)
    {
        ImGui::Separator();
        if (ImGui::Button("3D Build in Scene (Procedural)", ImVec2(-1, 40)))
        {
            // ★修正: _worldPtr ではなく context.world を使用
            if (context.world)
            {
                ID3D11Device* device = Graphics::Instance().GetDevice();
                if (_clearBeforeBuild) {
                    DungeonLevelBuilder::Clear(*context.world);
                }
                DungeonLevelBuilder::BuildParams p;
                p.seed = static_cast<uint32_t>(_seed);
                p.floorIndex = _floor;
                p.gen = _gen;
                DungeonLevelBuilder::BuildFromResult(*context.world, device, _lastResult, p);
            }
            else
            {
                CCL_LOG_ERROR(LogCategory::Editor, "[DungeonEditor] context.world is null!");
            }
        }

        // プレビュー用キャンバスの描画
        DrawProceduralCanvas();
    }
}

void DungeonGeneratorWindow::DrawProceduralCanvas()
{
    ImVec2 canvasSize(_lastResult.width * _zoom, _lastResult.depth * _zoom);
    ImGui::BeginChild("ProcCanvas", canvasSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 描画ループ
    for (int j = 0; j < _lastResult.depth; ++j)
    {
        for (int i = 0; i < _lastResult.width; ++i)
        {
            int rj = (_lastResult.depth - 1) - j; // Y反転描画
            ImVec2 pmin(origin.x + i * _zoom, origin.y + rj * _zoom);
            ImVec2 pmax(pmin.x + _zoom, pmin.y + _zoom);

            ImU32 color = _lastResult.solid[i][j] ? IM_COL32(100, 100, 100, 255) : IM_COL32(30, 30, 30, 255);

            if (i == _lastResult.startCell.i && j == _lastResult.startCell.j) color = IM_COL32(0, 255, 0, 255);
            else if (i == _lastResult.portalCell.i && j == _lastResult.portalCell.j) color = IM_COL32(255, 0, 255, 255);

            draw->AddRectFilled(pmin, pmax, color);
            draw->AddRect(pmin, pmax, IM_COL32(50, 50, 50, 255));
        }
    }

    // マーカー類（敵や宝箱）
    for (const auto& room : _lastResult.rooms) {
        for (const auto& m : room.spawnMarkers) {
            int rj = (_lastResult.depth - 1) - m.cell.j;
            ImVec2 pmin(origin.x + m.cell.i * _zoom, origin.y + rj * _zoom);
            ImVec2 pmax(pmin.x + _zoom, pmin.y + _zoom);
            ImU32 color = IM_COL32(255, 100, 100, 255);
            if (m.type == SpawnMarkerType::Chest) color = IM_COL32(255, 255, 0, 255);
            draw->AddRectFilled(pmin, pmax, color);
        }
    }

    ImGui::EndChild();
}


// =========================================================================
//  手作りマップ（Hand-Made）タブ
// =========================================================================
void DungeonGeneratorWindow::DrawHandMadeTab(EditorContext& context)
{
    ImGui::Spacing();

    // --- グループ1: ファイル I/O ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[ File Storage ]");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
    ImGui::InputText("Map Path", _savePath, sizeof(_savePath), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Map"))
    {
        if (auto pathOpt = Engine::FileDialog::OpenPreset("Scene")) {
            std::string relPath = Engine::FileDialog::ConvertToRelativePath(*pathOpt);
            std::snprintf(_savePath, sizeof(_savePath), "%s", relPath.c_str());
        }
    }

    if (ImGui::Button("Save Layout", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0))) {
        _layout.SaveToJson(_savePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Layout", ImVec2(-1, 0))) {
        if (_layout.LoadFromJson(_savePath)) {
            _resizeW = _layout.width;
            _resizeH = _layout.depth;
        }
    }
    ImGui::EndGroup();

    ImGui::Separator();

    // --- グループ2: マップサイズと初期化 ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[ Canvas Dimension ]");
    ImGui::InputInt("Width", &_resizeW);
    ImGui::InputInt("Depth", &_resizeH);
    _resizeW = (std::max)(3, _resizeW);
    _resizeH = (std::max)(3, _resizeH);

    if (ImGui::Button("Reset & Clear Canvas", ImVec2(-1, 25)))
    {
        // 手動リセット処理
        _layout.width = _resizeW;
        _layout.depth = _resizeH;
        _layout.solid.assign(_resizeW, std::vector<bool>(_resizeH, false));

        // 外周だけ壁にする
        for (int i = 0; i < _resizeW; ++i) {
            _layout.solid[i][0] = true;
            _layout.solid[i][_resizeH - 1] = true;
        }
        for (int j = 0; j < _resizeH; ++j) {
            _layout.solid[0][j] = true;
            _layout.solid[_resizeW - 1][j] = true;
        }

        _layout.startCell = { 1, 1 };
        _layout.portalCell = { _resizeW - 2, _resizeH - 2 };
        _layout.markers.clear();

        // ★ 敵の自動シード（デバッグ検証用）
        int centerI = _resizeW / 2;
        int centerJ = _resizeH / 2;
        if (centerI == 1 && centerJ == 1) centerI = _resizeW - 2; // 安全策

        _layout.solid[centerI][centerJ] = false;
        _layout.markers.push_back({ { centerI, centerJ }, SpawnMarkerType::Enemy, std::string(_currentEnemyPrefab) });

        CCL_LOG_INFO(LogCategory::Editor, "[Editor] Map initialized. Auto-seeded a debug enemy at (%d, %d)", centerI, centerJ);
    }
    ImGui::EndGroup();

    ImGui::Separator();

    // --- グループ3: ペイントツール ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[ Brush Palette ]");
    ImGui::RadioButton("Wall / Floor", &_editTool, 0); ImGui::SameLine();
    ImGui::RadioButton("Player Start", &_editTool, 1); ImGui::SameLine();
    ImGui::RadioButton("Escape Portal", &_editTool, 2); ImGui::SameLine();
    ImGui::RadioButton("Enemy Pin", &_editTool, 3); ImGui::SameLine();
    ImGui::RadioButton("Chest", &_editTool, 4);

    if (_editTool == 3)
    {
        ImGui::Indent(20.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
        ImGui::InputText("Target Prefab", _currentEnemyPrefab, sizeof(_currentEnemyPrefab), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("Browse...##Enemy"))
        {
            if (auto pathOpt = Engine::FileDialog::OpenPreset("Prefab")) {
                std::string relPath = Engine::FileDialog::ConvertToRelativePath(*pathOpt);
                std::snprintf(_currentEnemyPrefab, sizeof(_currentEnemyPrefab), "%s", relPath.c_str());
            }
        }
        ImGui::Unindent(20.0f);
    }
    ImGui::EndGroup();

    ImGui::Separator();

    // --- グループ4: 3Dビルド実行 ---
    if (ImGui::Button("3D Build in Scene (Hand-Made)", ImVec2(-1, 40)))
    {
        if (context.world)
        {
            ID3D11Device* device = Graphics::Instance().GetDevice();
            if (_clearBeforeBuild) {
                DungeonLevelBuilder::Clear(*context.world);
            }

            DungeonLevelBuilder::BuildParams p;
            p.seed = 999;
            p.floorIndex = 1;
            p.spawnPrefabs = true;

            DungeonGenerateResult res = _layout.ToResult(p.seed);
            DungeonLevelBuilder::BuildFromResult(*context.world, device, res, p);
        }
        else
        {
            CCL_LOG_ERROR(LogCategory::Editor, "[DungeonEditor] context.world is null!");
        }
    }

    ImGui::Separator();
    ImGui::Text("Stress Test:");
    ImGui::SliderInt("Swarm Count", &_swarmTestCount, 0, 5000);

    if (ImGui::Button("Swarm Test", ImVec2(-1, 40)))
    {
        // ★ ビルド完了直後に UI からの指示数でスウォームを発生させる
        if (_swarmTestCount > 0)
        {
            ID3D11Device* debug = nullptr;
            SceneInitializer::SpawnEnemySwarmTest(*context.world, debug, _swarmTestCount);
        }
    }

    // ペイント用キャンバスの描画
    DrawHandMadeCanvas();
}

void DungeonGeneratorWindow::DrawHandMadeCanvas()
{
    auto& L = _layout;
    if (L.width <= 0 || L.depth <= 0 || L.solid.empty()) return;

    ImVec2 canvasSize(L.width * _zoom, L.depth * _zoom);
    ImGui::BeginChild("HandMadeCanvas", canvasSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // --- 1. グリッドの描画 ---
    for (int j = 0; j < L.depth; ++j)
    {
        for (int i = 0; i < L.width; ++i)
        {
            int rj = (L.depth - 1) - j; // Z軸を上方向にするためのY反転
            ImVec2 pmin(origin.x + i * _zoom, origin.y + rj * _zoom);
            ImVec2 pmax(pmin.x + _zoom, pmin.y + _zoom);

            ImU32 color = L.solid[i][j] ? IM_COL32(100, 100, 100, 255) : IM_COL32(30, 30, 30, 255);

            if (i == L.startCell.i && j == L.startCell.j) color = IM_COL32(0, 255, 0, 255);
            else if (i == L.portalCell.i && j == L.portalCell.j) color = IM_COL32(255, 0, 255, 255);

            draw->AddRectFilled(pmin, pmax, color);
            draw->AddRect(pmin, pmax, IM_COL32(50, 50, 50, 255));
        }
    }

    // --- 2. マーカーの描画 ---
    for (const auto& m : L.markers) {
        int rj = (L.depth - 1) - m.cell.j;
        ImVec2 pmin(origin.x + m.cell.i * _zoom, origin.y + rj * _zoom);
        ImVec2 pmax(pmin.x + _zoom, pmin.y + _zoom);
        ImU32 color = IM_COL32(255, 0, 0, 255); // 敵は赤
        if (m.type == SpawnMarkerType::Chest) color = IM_COL32(255, 255, 0, 255); // 宝箱は黄
        draw->AddRectFilled(pmin, pmax, color);
    }

    // --- 3. 当たり判定の生成（これが抜けていました） ---
    // 描画領域と完全に一致する「透明なボタン」を配置し、マウスイベントを正確に捕捉します
    ImGui::InvisibleButton("##CanvasHitbox", canvasSize);

    // --- 4. マウスインタラクション（ペイントロジック） ---
    if (ImGui::IsItemHovered())
    {
        ImVec2 mp = ImGui::GetMousePos();
        int ci = (int)((mp.x - origin.x) / _zoom);
        int rj = (int)((mp.y - origin.y) / _zoom);
        int cj = (L.depth - 1) - rj; // Y反転

        if (L.InBounds(ci, cj))
        {
            // ==========================================================
            // ▼ ユニバーサル消しゴム機能（右ドラッグ・長押し対応）
            // ==========================================================
            // どのツールが選択されていても、右クリックで「床に戻す＆マーカーを消去」を保証する
            if (ImGui::IsMouseDown(1))
            {
                L.solid[ci][cj] = false; // 壁を削って床にする

                // 対象セルに存在するすべてのマーカー（敵・宝箱など）を配列から削除する
                L.markers.erase(
                    std::remove_if(L.markers.begin(), L.markers.end(),
                        [ci, cj](const SpawnMarker& m) { return m.cell.i == ci && m.cell.j == cj; }),
                    L.markers.end()
                );
            }

            // ==========================================================
            // ▼ ペイント機能（左ドラッグ・長押し対応）
            // ==========================================================
            else if (ImGui::IsMouseDown(0)) // 右クリック中は左ドラッグを無視する安全策
            {
                switch (_editTool)
                {
                case 0: L.solid[ci][cj] = true; break; // 壁を塗る
                case 1: L.startCell = { ci, cj }; L.solid[ci][cj] = false; break;
                case 2: L.portalCell = { ci, cj }; L.solid[ci][cj] = false; break;
                }
            }

            // ==========================================================
            // ▼ マーカー配置機能（単発左クリックのみ。ドラッグによる無限生成防止）
            // ==========================================================
            if (ImGui::IsMouseClicked(0))
            {
                if (_editTool == 3 || _editTool == 4)
                {
                    L.solid[ci][cj] = false;

                    // ★ 多重配置防止の安全装置
                    // 同じマスに連打して敵が何十匹も重なって生成されるのを防ぐため、
                    // 新しく置く前にそのマスの古いマーカーを削除（上書き）する。
                    L.markers.erase(
                        std::remove_if(L.markers.begin(), L.markers.end(),
                            [ci, cj](const SpawnMarker& m) { return m.cell.i == ci && m.cell.j == cj; }),
                        L.markers.end()
                    );

                    // 新しいマーカーを登録
                    if (_editTool == 3) { // Enemy
                        L.markers.push_back({ { ci, cj }, SpawnMarkerType::Enemy, std::string(_currentEnemyPrefab) });
                    }
                    else if (_editTool == 4) { // Chest
                        L.markers.push_back({ { ci, cj }, SpawnMarkerType::Chest, "" });
                    }
                }
            }
        }
    }

    ImGui::EndChild();
}