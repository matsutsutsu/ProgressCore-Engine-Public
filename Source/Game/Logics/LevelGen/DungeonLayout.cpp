//============================================================================
//  DungeonLayout.cpp
//  手作りマップの JSON 入出力（nlohmann::json + FileSystem）。
//============================================================================
#include "DungeonLayout.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <json.hpp>
using nlohmann::json;

namespace RuneForge::Dungeon
{
    //------------------------------------------------------------------------
    //  JSON 形式
    //  {
    //    "width": 21, "depth": 21, "cellSize": 2.0, "originX": 0, "originZ": 0,
    //    "start": [1,1], "portal": [19,19],
    //    "solid": ["#....#", ...],          // 1行=Z固定でX並び。'#'=壁 '.'=床
    //    "markers": [ {"i":3,"j":3,"type":"Enemy"}, ... ]
    //  }
    //  solid を文字列の配列にすることで人間が手で編集しやすくしている。
    //------------------------------------------------------------------------

    static const char* MarkerTypeToStr(SpawnMarkerType t)
    {
        switch (t)
        {
        case SpawnMarkerType::Enemy:      return "Enemy";
        case SpawnMarkerType::Chest:      return "Chest";
        case SpawnMarkerType::Portal:     return "Portal";
        case SpawnMarkerType::Boss:       return "Boss";
        case SpawnMarkerType::Shop:       return "Shop";
        case SpawnMarkerType::Forge:      return "Forge";
        case SpawnMarkerType::StartPoint: return "StartPoint";
        }
        return "Enemy";
    }

    static SpawnMarkerType StrToMarkerType(const std::string& s)
    {
        if (s == "Enemy")      return SpawnMarkerType::Enemy;
        if (s == "Chest")      return SpawnMarkerType::Chest;
        if (s == "Portal")     return SpawnMarkerType::Portal;
        if (s == "Boss")       return SpawnMarkerType::Boss;
        if (s == "Shop")       return SpawnMarkerType::Shop;
        if (s == "Forge")      return SpawnMarkerType::Forge;
        if (s == "StartPoint") return SpawnMarkerType::StartPoint;
        return SpawnMarkerType::Enemy;
    }

    bool DungeonLayout::SaveToJson(const std::string& virtualPath) const
    {
        json j;
        j["width"] = width;
        j["depth"] = depth;
        j["cellSize"] = cellSize;
        j["originX"] = originX;
        j["originZ"] = originZ;
        j["start"] = { startCell.i,  startCell.j };
        j["portal"] = { portalCell.i, portalCell.j };

        // solid を行文字列で（j を固定して i を並べる。上から depth-1..0 で人間が読める向き）
        json rows = json::array();
        for (int jj = depth - 1; jj >= 0; --jj)
        {
            std::string row;
            row.reserve(width);
            for (int ii = 0; ii < width; ++ii)
                row.push_back(solid[ii][jj] ? '#' : '.');
            rows.push_back(row);
        }
        j["solid"] = rows;

        json ms = json::array();
        for (const auto& m : markers)
        {
            json mj;
            mj["i"] = m.cell.i;
            mj["j"] = m.cell.j;
            mj["type"] = MarkerTypeToStr(m.type);

            // ★追加: カスタムプレハブが設定されていれば保存
            if (!m.customPrefab.empty()) {
                mj["prefab"] = m.customPrefab;
            }
            ms.push_back(mj);
        }
        j["markers"] = ms;

        const std::string text = j.dump(2);
        if (!FileSystem::WriteAllText(virtualPath, text))
        {
            CCL_LOG_ERROR(LogCategory::Core, "[DungeonLayout] Save failed: %s", virtualPath.c_str());
            return false;
        }
        CCL_LOG_INFO(LogCategory::Core, "[DungeonLayout] Saved: %s", virtualPath.c_str());
        return true;
    }

    bool DungeonLayout::LoadFromJson(const std::string& virtualPath)
    {
        if (!FileSystem::Exists(virtualPath))
        {
            CCL_LOG_WARN(LogCategory::Core, "[DungeonLayout] Not found: %s", virtualPath.c_str());
            return false;
        }

        const std::string text = FileSystem::ReadAllText(virtualPath);
        if (text.empty())
        {
            CCL_LOG_ERROR(LogCategory::Core, "[DungeonLayout] Empty file: %s", virtualPath.c_str());
            return false;
        }

        json j;
        try { j = json::parse(text); }
        catch (const std::exception& e)
        {
            CCL_LOG_ERROR(LogCategory::Core, "[DungeonLayout] JSON parse error: %s (%s)",
                virtualPath.c_str(), e.what());
            return false;
        }

        int w = j.value("width", 21);
        int h = j.value("depth", 21);
        Reset(w, h); // サイズ確定＋外周壁で初期化（このあと solid を上書き）

        cellSize = j.value("cellSize", 2.0f);
        originX = j.value("originX", 0.0f);
        originZ = j.value("originZ", 0.0f);

        if (j.contains("start") && j["start"].is_array() && j["start"].size() == 2)
            startCell = { j["start"][0].get<int>(), j["start"][1].get<int>() };
        if (j.contains("portal") && j["portal"].is_array() && j["portal"].size() == 2)
            portalCell = { j["portal"][0].get<int>(), j["portal"][1].get<int>() };

        // solid 行文字列を読み戻す（保存と同じ向き: 先頭行が j=depth-1）
        if (j.contains("solid") && j["solid"].is_array())
        {
            const auto& rows = j["solid"];
            for (int r = 0; r < (int)rows.size(); ++r)
            {
                int jj = depth - 1 - r;
                if (jj < 0) break;
                const std::string row = rows[r].get<std::string>();
                for (int ii = 0; ii < width && ii < (int)row.size(); ++ii)
                    solid[ii][jj] = (row[ii] == '#');
            }
        }

        markers.clear();
        if (j.contains("markers") && j["markers"].is_array())
        {
            for (const auto& mj : j["markers"])
            {
                SpawnMarker m;
                m.cell.i = mj.value("i", 0);
                m.cell.j = mj.value("j", 0);
                m.type = StrToMarkerType(mj.value("type", std::string("Enemy")));
                // JSONにプレハブ指定があれば読み込む
                m.customPrefab = mj.value("prefab", std::string(""));
                markers.push_back(m);
            }
        }

        CCL_LOG_INFO(LogCategory::Core, "[DungeonLayout] Loaded: %s (%dx%d, %zu markers)",
            virtualPath.c_str(), width, depth, markers.size());
        return true;
    }

} // namespace RuneForge::Dungeon