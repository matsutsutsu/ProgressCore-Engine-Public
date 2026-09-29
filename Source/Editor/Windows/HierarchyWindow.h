#pragma once
// ===========================================================================
// 概要: シーン内のエンティティを親子の階層で一覧表示するウィンドウ。
//
// 責務:
//   - 開いているノードだけをたどって一覧を作り、見えている行だけを描く
//   - 名前での検索、選択（Ctrl で追加、Shift で範囲）
//   - ドラッグ＆ドロップでの親子付け・親の解除と、プレハブ・モデルの配置
//   - 複製（Ctrl+D）・削除・親の解除の右クリックメニュー
//
// 備考: 一覧は毎フレーム作り直す。開閉状態はこのウィンドウが m_openNodes で持つ。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class HierarchyWindow : public EditorWindow
{
public:
    HierarchyWindow();

protected:
    void DrawContents(EditorContext &context) override;

private:
    // 一覧の1行
    struct FlatNode
    {
        CCL::ECS::EntityID entityID;
        int                depth;       // 字下げの段数（ルートが 0）
        bool               hasChildren; // 子を持つか（開閉の矢印を出すか）
    };

    std::unordered_set<CCL::ECS::EntityID> m_openNodes; // 開いているノード

    // 毎フレーム作り直すが、確保し直さないようメンバに持つ
    std::vector<CCL::ECS::EntityID> m_allEntities; // 検索中だけ使う全エンティティ
    std::vector<FlatNode>           m_flatList;    // 表示する行（表示順）

    // firstChildID から兄弟をたどって一覧に足す。開いている子はさらに潜る
    void FlattenChildren(
        CCL::ECS::EntityID     firstChildID,
        int                    depth,
        CCL::ECS::World       *world,
        std::vector<FlatNode> &outFlatList);

    void DrawFlatNode(
        const FlatNode &node,
        EditorContext  &context);

    // Shift+クリックの範囲選択。m_flatList の並びで、主選択から clicked までを選ぶ
    void SelectRangeTo(CCL::ECS::EntityID clicked, EditorContext &context);

    // 選択中のエンティティを階層ごと複製し、複製したものを選択する（Ctrl+D と右クリックメニュー）
    void DuplicateSelection(EditorContext &context);

    // targetEntity が ancestorEntity 自身か、その子孫か
    bool IsDescendantOf(
        CCL::ECS::EntityID targetEntity,
        CCL::ECS::EntityID ancestorEntity,
        CCL::ECS::World   *world);

    char m_searchBuffer[256] = {0};
};
