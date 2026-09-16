#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Transform/Components/TransformComponent.h"

class HierarchyCleanupSystem : public CCL::ECS::IfSystem<
                                   HierarchyCleanupSystem,
                                   CCL::ECS::Read<Tag::DestroyTag>>
{
public:
    HierarchyCleanupSystem();
    virtual ~HierarchyCleanupSystem() = default;

    // フレームの最後に実行され、タグ付きエンティティと子供を回収する
    void Update(float dt) override;

    // 外部（SceneManager等）から強制的にクリーンアップを走らせる関数
    static void ExecuteCleanup(CCL::ECS::World &world);

    /**
     * @brief このフレームで消えるエンティティを全て集める（DestroyTag の起点＋その子孫）。
     *
     * @details
     *  DestroyTag は「削除の起点」にしか付かない（World::Destroy は引数の1体にしか
     *  付けない）。一方このシステムは子孫まで RequestDestroyEntity するため、
     *  「DestroyTag が付いている」と「消える」は一致しない。
     *
     *  外部リソース（Jolt剛体・再生中サウンド）を解放するクリーンアップが
     *  DestroyTag だけを見ると、子エンティティの分を取りこぼして永久にリークする。
     *  そのため「誰が消えるか」の判定はこの関数に一本化し、
     *  JoltCleanupSystem / AudioCleanupSystem もこれを使う。
     *
     * @note 結果はソート済み・重複排除済み。
     */
    static void CollectDoomedEntities(
        CCL::ECS::World &world, std::vector<CCL::ECS::EntityID> &outList);

    // 実行順の宣言。外部リソースの解放を済ませてから ECS の構造を壊す。
    //   （同一ステージ内の順序は必ず RunAfter で宣言する。ステージ値では決まらない）
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCleanupSystem", "AudioCleanupSystem"};
    }

private:
    // 再帰的に子供のIDをリストに追加するヘルパー関数
    // static関数から呼べるように、world を引数で受け取るようにする
    static void CollectChildrenRecursive(
        CCL::ECS::World &world, CCL::ECS::EntityID currentID, std::vector<CCL::ECS::EntityID> &outList);
};
