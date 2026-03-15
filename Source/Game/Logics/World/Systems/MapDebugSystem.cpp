#include "MapDebugSystem.h"
#include "Game/Logics/World/LevelLoader.h"
#include "Game/Logics/World/StageMap.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <iostream>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// JSONライブラリ
#include <json.hpp>
using json = nlohmann::json;

namespace fs = std::filesystem;

MapDebugSystem::MapDebugSystem() : SystemBase("MapDebugSystem")
{
    // 初期設定としてステージ1の設定をロードしておく
    _genConfig = MapGenerator::GetStageConfig(1);

    // 起動時にファイル一覧を更新
    ScanTemplateFiles();
}

void MapDebugSystem::SetDevice(ID3D11Device *device) { _device = device; }

void MapDebugSystem::OnDrawDebug(ShapeRenderer *renderer)
{
    if (!_world || !_world->HasResource<StageMap>() || !renderer || !this->isDebugVisible) return;
    const auto &map = _world->GetResource<StageMap>();

    // 1. 既存の壁表示 (高さ1.0f)
    DirectX::SimpleMath::Vector3 size(map.cellSize * 0.5f, 1.0f, map.cellSize * 0.5f);
    DirectX::SimpleMath::Vector4 colorWall(1.0f, 0.2f, 0.2f, 0.5f);

    for (int z = 0; z < map.height; ++z) {
        for (int x = 0; x < map.width; ++x) {
            if (map.IsWall(x, z)) {
                DirectX::SimpleMath::Vector3 center = map.GridToWorld(x, z);
                center.y                            = 1.0f;
                renderer->DrawBox(center, DirectX::SimpleMath::Vector3::Zero, size, colorWall);
            }
        }
    }

    // 2. 部屋範囲の表示 (高さ5.0f)
    for (const auto &room : map.debugRooms) {
        float worldX = (room.x + room.w * 0.5f) * map.cellSize;
        float worldZ = (room.y + room.h * 0.5f) * map.cellSize;
        float halfW  = (room.w * map.cellSize) * 0.5f;
        float halfH  = (room.h * map.cellSize) * 0.5f;

        DirectX::SimpleMath::Vector3 center(worldX, 5.0f, worldZ);
        DirectX::SimpleMath::Vector3 roomSize(halfW, 1.0f, halfH);

        // 色分け
        DirectX::SimpleMath::Vector4 color;
        if (room.type == 0)
            color = {0.0f, 1.0f, 0.0f, 0.5f}; // Start
        else if (room.type == 3)
            color = {1.0f, 0.0f, 0.0f, 0.5f}; // Boss
        else if (room.w >= 20 || room.h >= 20)
            color = {1.0f, 0.0f, 1.0f, 0.4f}; // Large
        else
            color = {0.0f, 1.0f, 1.0f, 0.4f}; // Normal

        renderer->DrawBox(center, DirectX::SimpleMath::Vector3::Zero, roomSize, color);
    }

    // 3. エディタのカーソル枠表示 (高さ2.5f)
    int                          startX = targetGridX - currentW / 2;
    int                          startZ = targetGridZ - currentH / 2;
    DirectX::SimpleMath::Vector4 colorFrame(1.0f, 1.0f, 0.0f, 0.8f);

    for (int y = 0; y < currentH; ++y) {
        for (int x = 0; x < currentW; ++x) {
            // 塗られている場所または四隅を表示
            if (editRoom[y][x] != 0 || (x == 0 && y == 0) || (x == currentW - 1 && y == 0) ||
                (x == 0 && y == currentH - 1) || (x == currentW - 1 && y == currentH - 1)) {

                int                          mx     = startX + x;
                int                          mz     = startZ + y;
                DirectX::SimpleMath::Vector3 center = map.GridToWorld(mx, mz);
                center.y                            = 2.5f;
                renderer->DrawBox(
                    center, DirectX::SimpleMath::Vector3::Zero, size * 0.9f, colorFrame);
            }
        }
    }
}

void MapDebugSystem::OnGui()
{
    if (ImGui::BeginTabBar("MapDebugTabs")) {

        // タブ1: 部屋テンプレートエディタ
        if (ImGui::BeginTabItem("Room Editor")) {
            DrawRoomEditor();
            ImGui::EndTabItem();
        }

        // タブ2: マップ生成テスト
        if (ImGui::BeginTabItem("Generator Config")) {
            DrawGeneratorConfig();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void MapDebugSystem::Update(float dt)
{
    if (_requestRegenerate) {
        ExecuteRegeneration();
        _requestRegenerate = false;
    }
}

void MapDebugSystem::DrawRoomEditor()
{
    ImGui::Begin("MapEditor");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "1. File Operations");

    // ファイル一覧更新 & コンボボックス
    if (ImGui::Button("Refresh File List")) {
        ScanTemplateFiles();
    }

    ImGui::SameLine();

    if (!_fileList.empty()) {
        std::vector<const char *> filesCStr;
        for (const auto &s : _fileList) filesCStr.push_back(s.c_str());

        ImGui::PushItemWidth(200);
        if (ImGui::Combo(
                "Select File", &_selectedFileIdx, filesCStr.data(), (int)filesCStr.size())) {
            // 選択したらファイル名入力欄にも反映
            strncpy_s(
                _exportFileName, _fileList[_selectedFileIdx].c_str(), sizeof(_exportFileName) - 1);
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();

        if (ImGui::Button("Load JSON")) {
            LoadTemplateFromJson();
        }
    }
    else {
        ImGui::Text("(No templates found)");
    }

    ImGui::Separator();
    ImGui::Text("Save Settings");

    // ファイル名入力
    ImGui::InputText("Filename", _exportFileName, sizeof(_exportFileName));

    // サイズ調整
    ImGui::DragInt("Width", &currentW, 1, 4, MAX_TEMPLATE_SIZE);
    ImGui::DragInt("Height", &currentH, 1, 4, MAX_TEMPLATE_SIZE);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "2. Paint");

    // パレット
    ImGui::RadioButton("Floor (0)", &selectedTile, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Wall  (1)", &selectedTile, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Hole  (2)", &selectedTile, 2);

    // クリアボタン
    if (ImGui::Button("Clear All Tiles")) {
        for (int y = 0; y < MAX_TEMPLATE_SIZE; ++y)
            for (int x = 0; x < MAX_TEMPLATE_SIZE; ++x) editRoom[y][x] = 0;
    }

    ImGui::Separator();


    ImGui::TextColored(ImVec4(0, 1, 1, 1), "3. Actions");

    if (ImGui::Button("Save to JSON", ImVec2(120, 30))) {
        SaveTemplateToJson();
        ScanTemplateFiles(); // 保存後にリスト更新
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();

    // テスト配置
    ImGui::PushItemWidth(100);
    ImGui::DragInt("Target X", &targetGridX, 1, 0, 100);
    ImGui::SameLine();
    ImGui::DragInt("Target Z", &targetGridZ, 1, 0, 100);
    ImGui::PopItemWidth();

    if (ImGui::Button("Test Apply (At Target)", ImVec2(180, 30))) {
        ApplyToMap();
    }


    ImGui::Separator();


    // グリッド描画
    ImGui::BeginChild("GridArea", ImVec2(0, 0), true);
    for (int y = 0; y < currentH; ++y) {
        for (int x = 0; x < currentW; ++x) {
            ImGui::PushID(y * MAX_TEMPLATE_SIZE + x);

            int    tile     = editRoom[y][x];
            ImVec4 btnColor = (tile == 1)   ? ImVec4(0.4f, 0.4f, 0.4f, 1.0f)  // Wall
                              : (tile == 2) ? ImVec4(0.1f, 0.0f, 0.0f, 1.0f)  // Hole
                                            : ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // Floor

            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);

            ImGui::Button("##G", ImVec2(20, 20));

            // ドラッグ対応
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                editRoom[y][x] = selectedTile;
            }

            ImGui::PopStyleColor();
            ImGui::PopID();

            if (x < currentW - 1) ImGui::SameLine();
        }
    }
    ImGui::EndChild();




    ImGui::End();
}

void MapDebugSystem::DrawGeneratorConfig()
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Stage Configuration");

    // プリセットロード機能
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Stage Index:");
    ImGui::SameLine();
    ImGui::InputInt("##StageIdx", &_targetStageIndex);

    if (ImGui::Button("Load Preset", ImVec2(-1, 0))) {
        _genConfig = MapGenerator::GetStageConfig(_targetStageIndex);
    }

    ImGui::Separator();
    ImGui::Text("Tweaking Parameters");

    ImGui::SliderInt("Map Width", &_genConfig.mapSizeW, 20, 200);
    ImGui::SliderInt("Map Height", &_genConfig.mapSizeH, 20, 200);
    ImGui::SliderInt("Room Count", &_genConfig.roomCount, 5, 50);

    ImGui::SliderInt("Corridor Width", &_genConfig.corridorWidth, 1, 5);
    ImGui::SliderInt("Corridor Length", &_genConfig.corridorLength, 1, 20);
    ImGui::SliderInt("Room Margin", &_genConfig.roomMargin, 0, 10);

    ImGui::SliderInt("Physics Iterations", &_genConfig.physicsIterations, 100, 2000);

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("REGENERATE MAP", ImVec2(-1, 50))) {
        _requestRegenerate = true;
    }
}

void MapDebugSystem::SaveTemplateToJson()
{
    json j;
    j["width"]  = currentW;
    j["height"] = currentH;
    std::vector<int> layout;
    layout.reserve(currentW * currentH);
    for (int y = 0; y < currentH; ++y) {
        for (int x = 0; x < currentW; ++x) {
            layout.push_back(editRoom[y][x]);
        }
    }
    j["layout"] = layout;

    std::string folderPath = "Assets/GamePlay/RoomTemplates";
    try {
        if (!fs::exists(folderPath)) {
            fs::create_directories(folderPath);
        }
    }
    catch (...) {
    }

    std::string filename = _exportFileName;
    if (filename.find(".json") == std::string::npos) filename += ".json";

    std::string   fullPath = folderPath + "/" + filename;
    std::ofstream o(fullPath);
    if (o.is_open()) {
        o << j.dump(4);
        std::cout << "[MapDebug] Saved: " << fullPath << std::endl;
    }
}

void MapDebugSystem::ScanTemplateFiles()
{
    _fileList.clear();
    std::string folderPath = "Assets/GamePlay/RoomTemplates";

    if (!fs::exists(folderPath)) return;

    for (const auto &entry : fs::directory_iterator(folderPath)) {
        if (entry.path().extension() == ".json") {
            _fileList.push_back(entry.path().stem().string());
        }
    }

    // インデックス調整
    if (_selectedFileIdx >= (int)_fileList.size()) _selectedFileIdx = 0;
}

void MapDebugSystem::LoadTemplateFromJson()
{
    if (_fileList.empty() || _selectedFileIdx < 0 || _selectedFileIdx >= (int)_fileList.size())
        return;

    std::string filename = _fileList[_selectedFileIdx] + ".json";
    std::string fullPath = "Assets/GamePlay/RoomTemplates/" + filename;

    std::ifstream i(fullPath);
    if (!i.is_open()) {
        std::cerr << "[MapDebug] Failed to load: " << fullPath << std::endl;
        return;
    }

    try {
        json j;
        i >> j;

        int w = j.value("width", 10);
        int h = j.value("height", 10);

        if (w > MAX_TEMPLATE_SIZE) w = MAX_TEMPLATE_SIZE;
        if (h > MAX_TEMPLATE_SIZE) h = MAX_TEMPLATE_SIZE;

        currentW = w;
        currentH = h;

        // グリッド初期化
        for (int y = 0; y < MAX_TEMPLATE_SIZE; ++y)
            for (int x = 0; x < MAX_TEMPLATE_SIZE; ++x) editRoom[y][x] = 0;

        // データ反映
        if (j.contains("layout") && j["layout"].is_array()) {
            const auto &layout = j["layout"];
            int         count  = 0;
            for (const auto &val : layout) {
                if (count >= w * h) break;
                int x          = count % w;
                int y          = count / w;
                editRoom[y][x] = val.get<int>();
                count++;
            }
        }

        std::cout << "[MapDebug] Loaded template: " << fullPath << std::endl;

        // 保存ファイル名も更新
        strncpy_s(
            _exportFileName, _fileList[_selectedFileIdx].c_str(), sizeof(_exportFileName) - 1);
    }
    catch (const std::exception &e) {
        std::cerr << "[MapDebug] JSON Parse Error: " << e.what() << std::endl;
    }
}

void MapDebugSystem::ApplyToMap()
{
    if (!_world || !_world->HasResource<StageMap>() || !_device) return;
    auto &map = const_cast<StageMap &>(_world->GetResource<StageMap>());

    int startX = targetGridX - currentW / 2;
    int startZ = targetGridZ - currentH / 2;

    for (int y = 0; y < currentH; ++y) {
        for (int x = 0; x < currentW; ++x) {
            int idx = map.GetIndex(startX + x, startZ + y);
            if (idx != -1) {
                int tile = editRoom[y][x];
                if (tile == 1)
                    map.tiles[idx] = TileType::Wall;
                else if (tile == 2)
                    map.tiles[idx] = TileType::Hole;
                else
                    map.tiles[idx] = TileType::Floor;
            }
        }
    }
    LevelLoader::Rebuild(_world, _device);
}

void MapDebugSystem::ExecuteRegeneration()
{
    if (!_world || !_world->HasResource<StageMap>() || !_device) return;
    auto &map = const_cast<StageMap &>(_world->GetResource<StageMap>());

    MapGenerator generator;
    if (generator.Generate(map, _genConfig)) {
        LevelLoader::Rebuild(_world, _device);
    }
}