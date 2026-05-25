#pragma once
#include <unordered_map>
#include <cstdint>
#include "Engine/ECS/Common/CCL_Common.h"

// ★ ロード（デシリアライズ）ごとに生成され、IDの翻訳を行うコンテキスト
class SerializationContext {
public:
    std::unordered_map<uint64_t, CCL::ECS::EntityID> idMap;

    void Clear() {
        idMap.clear();
    }

    // 読み込んだファイルのID(oldID) と、新しく確保した実体ID(newID) を紐付ける
    void RegisterID(uint64_t oldID, CCL::ECS::EntityID newID) {
        if (oldID != 0) {
            idMap[oldID] = newID;
        }
    }

    // 古いIDを渡して、現在の世界における正しい実体IDを返す
    CCL::ECS::EntityID ResolveID(uint64_t oldID) const {
        if (oldID == 0) return CCL::ECS::InvalidEntityID;

        auto it = idMap.find(oldID);
        if (it != idMap.end()) {
            return it->second;
        }
        return CCL::ECS::InvalidEntityID;
    }
};