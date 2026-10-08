#pragma once
// ===========================================================================
// 概要: DestroyTag を起点に、削除対象の Entity とその子孫を安全に破棄する System（L07）。
//
// 責務:
//   - 「このフレームで消える Entity」（DestroyTag の起点 + 子孫）の収集
//   - 親子リンクを外してから破棄を予約する（死んだ親の ID を子に残さない）
//
// 備考: 走査中に構造を変えると危険なので、先に消す対象を全て確定させてから一括で処理する。
// ===========================================================================
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

    // ロジックの最後（L07）に実行され、DestroyTag の付いた Entity と子孫を回収する
    void Update(float dt) override;

    // System の実行を待たずにクリーンアップを走らせる（SceneManager 等から呼ぶ）
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

    /**
     * @brief 実行順の宣言。外部リソースの解放を済ませてから ECS の構造を壊す。
     * @note 同一ステージ内の順序は必ず RunAfter で宣言する。ステージ値では決まらない。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCleanupSystem", "AudioCleanupSystem"};
    }

private:
    // 子孫の ID を再帰的に outList へ追加する
    static void CollectChildrenRecursive(
        CCL::ECS::World &world, CCL::ECS::EntityID currentID, std::vector<CCL::ECS::EntityID> &outList);
};
