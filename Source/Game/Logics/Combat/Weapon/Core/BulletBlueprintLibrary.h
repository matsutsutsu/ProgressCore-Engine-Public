#pragma once
#include "BulletBlueprint.h"
#include <memory>
#include <vector>

// ===========================================================================
// クラス名: BulletBlueprintLibrary
// 役割: 作成されたBlueprintを永続保持し、ポインタを提供する（Flyweight管理）
// ===========================================================================
class BulletBlueprintLibrary {
  public:
    // 設計図を登録し、そのポインタを返す
    // unique_ptrだとコピー不可でECSのResource登録時にエラーになるため、shared_ptrを使用
    const BulletBlueprint *Register(const BulletBlueprint &blueprint)
    {
        // ヒープに確保して共有ポインタにする
        auto                   ptr    = std::make_shared<BulletBlueprint>(blueprint);
        const BulletBlueprint *rawPtr = ptr.get();

        _blueprints.push_back(ptr);

        return rawPtr;
    }

    // シーン終了時などにリセット
    void Clear() { _blueprints.clear(); }

  private:
    // shared_ptrならコピー可能になる
    std::vector<std::shared_ptr<BulletBlueprint>> _blueprints;
};