/// @file  StateTreeSerializer.cpp
/// @brief StateTreeSerializer の実装。
#include "StateTreeSerializer.h"
#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"

#include "Engine/Platform/Core/FileSystem.h"                       // ReadAllText / WriteAllText
#include "Engine/Platform/Core/Logger.h"      // 読み込み失敗の報告

#include <cstring>   // memcpy

using namespace CCL::AI;
using json = nlohmann::json;

// ============================================================
// JSON → Def
// ============================================================
namespace
{
    NodeDef NodeFromJson(const json& j)
    {
        NodeDef n;
        n.type = j.value("type", std::string{});
        if (j.contains("params") && j["params"].is_object())
            n.params = j["params"];
        return n;
    }

    StateDef StateFromJson(const json& j)
    {
        StateDef s;
        s.name = j.value("name", std::string("State"));

        if (j.contains("enterConditions"))
            for (const auto& c : j["enterConditions"])
                s.enterConditions.push_back(NodeFromJson(c));

        if (j.contains("tasks"))
            for (const auto& t : j["tasks"])
                s.tasks.push_back(NodeFromJson(t));

        if (j.contains("children"))
            for (const auto& ch : j["children"])
                s.children.push_back(StateFromJson(ch));

        return s;
    }

    json NodeToJson(const NodeDef& n)
    {
        json j;
        j["type"] = n.type;
        if (!n.params.empty()) j["params"] = n.params;
        return j;
    }

    json StateToJson(const StateDef& s)
    {
        json j;
        j["name"] = s.name;
        if (!s.enterConditions.empty()) {
            json arr = json::array();
            for (const auto& c : s.enterConditions) arr.push_back(NodeToJson(c));
            j["enterConditions"] = arr;
        }
        if (!s.tasks.empty()) {
            json arr = json::array();
            for (const auto& t : s.tasks) arr.push_back(NodeToJson(t));
            j["tasks"] = arr;
        }
        if (!s.children.empty()) {
            json arr = json::array();
            for (const auto& ch : s.children) arr.push_back(StateToJson(ch));
            j["children"] = arr;
        }
        return j;
    }

    // --- params(json) → blob 書き込み（フィールド記述子で駆動）---
    void WriteParam(uint8_t *blob, const NodeParamField &f, const json &params)
    {
        switch (f.type)
        {
        case ParamType::Float: {
            float v = params.value(f.name, static_cast<float>(f.defVal));
            std::memcpy(blob + f.offset, &v, sizeof(float));
        }
        break;
        case ParamType::Int: {
            int v = params.value(f.name, static_cast<int>(f.defVal));
            std::memcpy(blob + f.offset, &v, sizeof(int));
        }
        break;
        case ParamType::Bool: {
            bool v = params.value(f.name, static_cast<bool>(f.defVal != 0.0));
            std::memcpy(blob + f.offset, &v, sizeof(bool));
        }
        break;
        // 文字列を固定長 char バッファへコピー
        case ParamType::String: {
            std::string v   = params.value(f.name, f.defStr);
            char       *dst = reinterpret_cast<char *>(blob + f.offset);
            if (f.capacity > 0)
            {
                strncpy_s(dst, f.capacity, v.c_str(), _TRUNCATE);
            }
        }
        break;
        }
    }

    // --- desc + params から asset.paramBlob に追記し、先頭オフセットを返す ---
    uint32_t BuildParamBlob(StateTreeAsset& asset, const NodeDesc* desc, const json& params)
    {
        if (!desc || desc->paramsSize == 0) return 0;

        std::vector<uint8_t> buf(desc->paramsSize, 0);   // ゼロ初期化（paddingも0に）
        for (const auto& f : desc->paramFields)
            WriteParam(buf.data(), f, params);

        const uint32_t offset = static_cast<uint32_t>(asset.paramBlob.size());
        asset.paramBlob.insert(asset.paramBlob.end(), buf.begin(), buf.end());
        return offset;
    }

    // --- Def の1状態を再帰的にコンパイル ---
    void CompileState(StateTreeAsset& asset, const StateDef& def, uint16_t parentIdx)
    {
        auto& reg = NodeRegistry::Instance();
        const uint16_t idx = asset.AddState(def.name, parentIdx);

        // 入場条件
        for (const auto& c : def.enterConditions) {
            const NodeTypeID id = reg.FindConditionByName(c.type);
            if (id == ST_INVALID_NODE_TYPE) {
                CCL_LOG_WARN(LogCategory::Game,
                    "[StateTree] 未知の条件種別 '%s' をスキップ", c.type.c_str());
                continue;
            }
            const NodeDesc* desc = reg.GetConditionDesc(id);
            const uint32_t off = BuildParamBlob(asset, desc, c.params);
            asset.AddEnterCondition(idx, id, off, desc ? desc->paramsSize : 0);
        }

        // タスク
        for (const auto& t : def.tasks) {
            const NodeTypeID id = reg.FindTaskByName(t.type);
            if (id == ST_INVALID_NODE_TYPE) {
                CCL_LOG_WARN(LogCategory::Game,
                    "[StateTree] 未知のタスク種別 '%s' をスキップ", t.type.c_str());
                continue;
            }
            const NodeDesc* desc = reg.GetTaskDesc(id);
            const uint32_t off = BuildParamBlob(asset, desc, t.params);
            asset.AddTask(idx, id, off,
                          desc ? desc->paramsSize : 0);
        }

        // 子
        for (const auto& ch : def.children)
            CompileState(asset, ch, idx);
    }
} // anonymous namespace


// ============================================================
// public API
// ============================================================

StateTreeDef StateTreeSerializer::FromJson(const json& j)
{
    StateTreeDef def;
    def.name = j.value("name", std::string("Untitled"));
    if (j.contains("root"))
        def.root = StateFromJson(j["root"]);
    else
        def.root.name = "Root";
    return def;
}

json StateTreeSerializer::ToJson(const StateTreeDef& def)
{
    json j;
    j["name"] = def.name;
    j["root"] = StateToJson(def.root);
    return j;
}

bool StateTreeSerializer::LoadDef(const std::string& virtualPath, StateTreeDef& out)
{
    if (!FileSystem::Exists(virtualPath)) {
        CCL_LOG_ERROR(LogCategory::Game, "[StateTree] ファイルが無い: %s", virtualPath.c_str());
        return false;
    }
    const std::string text = FileSystem::ReadAllText(virtualPath);
    if (text.empty()) return false;

    try {
        out = FromJson(json::parse(text));
    } catch (const std::exception& e) {
        CCL_LOG_ERROR(LogCategory::Game, "[StateTree] JSON parse 失敗: %s (%s)",
                      virtualPath.c_str(), e.what());
        return false;
    }
    return true;
}

bool StateTreeSerializer::SaveDef(const std::string& virtualPath, const StateTreeDef& def)
{
    const json j = ToJson(def);
    return FileSystem::WriteAllText(virtualPath, j.dump(2)); // インデント2で整形
}

StateTreeAsset StateTreeSerializer::Compile(const StateTreeDef& def)
{
    StateTreeAsset asset;
    asset.name = def.name;
    CompileState(asset, def.root, ST_INVALID);   // root を index 0 として追加
    return asset;
}
