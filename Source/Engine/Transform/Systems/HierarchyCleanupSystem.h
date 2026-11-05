#pragma once
// ===========================================================================
// 概要: DestroyTag を起点に、削除対象の Entity とその子孫を安全に破棄する System（L07）
//
// 責務:
//   - 「このフレームで消える Entity」（DestroyTag の起点 + 子孫）の収集
//   - 親子リンクを外してから破棄を予約する（死んだ親の ID を子に残さない）
//
// 備考: 走査中に構造を変えると危険なので、先に消す対象をすべて確定させてから一括で処理する
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
    HierarchyCleanupSystem() : IfSystem("HierarchyCleanupSystem") {}
    virtual ~HierarchyCleanupSystem() = default;

    // ロジックの最後（L07）に実行され、DestroyTag の付いた Entity と子孫を回収する
    void Update(float dt) override;

    // System の実行を待たずにクリーンアップを走らせる（SceneManager 等から呼ぶ）
    static void ExecuteCleanup(CCL::ECS::World &world);

    // このフレームで消える Entity（DestroyTag の起点とその子孫）を outList の末尾へ足す。結果はソート済みで重複なし
    // DestroyTag は起点にしか付かないので、外部のリソースを解放する処理が DestroyTag だけを見ると、子の分がリークする
    // そのため消える Entity はこの関数で求める（JoltCleanupSystem / AudioCleanupSystem も使う）
    static void CollectDoomedEntities(
        CCL::ECS::World &world, std::vector<CCL::ECS::EntityID> &outList);

    // 外部のリソースを解放してから、ECS の構造を壊す
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCleanupSystem", "AudioCleanupSystem"};
    }

private:
    // 子孫の ID を再帰的に outList へ追加する
    static void CollectChildrenRecursive(
        CCL::ECS::World &world, CCL::ECS::EntityID currentID, std::vector<CCL::ECS::EntityID> &outList);
};
