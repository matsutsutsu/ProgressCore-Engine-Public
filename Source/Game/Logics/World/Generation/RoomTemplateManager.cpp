#include "RoomTemplateManager.h"
#include <filesystem>
#include <fstream>
#include <iostream>

void RoomTemplateManager::Clear()
{
    _stageTemplates.clear();
    _templateCache.clear();
}

void RoomTemplateManager::RegisterTemplate(int stageIndex, const std::string &path)
{
    // 1. キャッシュになければロードする
    if (_templateCache.find(path) == _templateCache.end()) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[TemplateManager] Failed to open: " << path << std::endl;
            return;
        }

        try {
            json j;
            file >> j;

            RoomTemplate temp;
            temp.width  = j.value("width", 10);
            temp.height = j.value("height", 10);

            if (j.contains("layout")) {
                for (const auto &tile : j["layout"]) {
                    temp.tiles.push_back(tile.get<int>());
                }
            }

            // キャッシュに保存
            _templateCache[path] = temp;
            // std::cout << "[TemplateManager] Loaded new: " << path << std::endl;
        }
        catch (const std::exception &e) {
            std::cerr << "[TemplateManager] JSON Error: " << path << " : " << e.what() << std::endl;
            return;
        }
    }

    // 2. 指定ステージのリストに、キャッシュ上のアドレスを追加
    // これにより、データの実体は1つだけで済む（使い回しOK）
    _stageTemplates[stageIndex].push_back(&_templateCache[path]);
}

const RoomTemplate *RoomTemplateManager::GetRandomTemplate(int stageIndex, int w, int h)
{
    if (_stageTemplates.find(stageIndex) == _stageTemplates.end()) return nullptr;

    const auto                       &list = _stageTemplates[stageIndex];
    std::vector<const RoomTemplate *> candidates;

    // サイズ一致チェック
    for (const auto *t : list) {
        if (t->width == w && t->height == h) {
            candidates.push_back(t);
        }
    }

    if (candidates.empty()) return nullptr;

    std::uniform_int_distribution<int> dist(0, (int)candidates.size() - 1);
    return candidates[dist(_gen)];
}