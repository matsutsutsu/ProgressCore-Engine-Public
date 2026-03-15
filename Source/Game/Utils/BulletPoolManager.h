#pragma once
#include "ECS/Core/CCL_World.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace CCL::ECS;

// ★新規追加: 取得結果を返す構造体
struct BulletPoolResult {
    EntityID id;
    bool     isNewlyCreated; // trueなら新規作成、falseならプールからの再利用
};

class BulletPoolManager {
  public:
    // 文字列ではなくハッシュ値で検索する
    BulletPoolResult WakeOrSpawn(Core::World &world, uint32_t keyHash, const Archetype &arch);

    // 使い終わった弾をプールに戻す
    void ReturnToPool(Core::World &world, EntityID id);

  private:
    // std::string を uint32_t に変更し、ハッシュマップの負荷を激減させる
    std::unordered_map<uint32_t, std::vector<EntityID>> _poolMap;
};