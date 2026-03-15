#pragma once
#include <json.hpp>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

struct RoomTemplate {
    int              width  = 0;
    int              height = 0;
    std::vector<int> tiles;
    int              Get(int x, int y) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height) return 1;
        return tiles[y * width + x];
    }
};

class RoomTemplateManager {
  public:
    static RoomTemplateManager &Instance()
    {
        static RoomTemplateManager instance;
        return instance;
    }

    // 手動登録関数
    // path: JSONファイルのパス (例: "Assets/GamePlay/RoomTemplates/MyRoom.json")
    void RegisterTemplate(int stageIndex, const std::string &path);

    // 登録情報をリセット（シーン遷移時などに呼ぶ）
    void Clear();

    const RoomTemplate *GetRandomTemplate(int stageIndex, int w, int h);

  private:
    RoomTemplateManager() = default;

    // ステージごとのテンプレートリスト
    std::unordered_map<int, std::vector<const RoomTemplate *>> _stageTemplates;

    // ファイルキャッシュ (パス -> 実体)
    // 実体はここで一元管理し、_stageTemplates はポインタを持つだけにする
    std::unordered_map<std::string, RoomTemplate> _templateCache;

    std::mt19937 _gen{std::random_device{}()};
};