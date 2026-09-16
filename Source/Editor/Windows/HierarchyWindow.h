#pragma once
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
    struct FlatNode
    {
        CCL::ECS::EntityID entityID;
        int                depth;
        bool               hasChildren;
    };

    std::unordered_set<CCL::ECS::EntityID> m_openNodes;

    // 毎フレームのメモリ確保を防ぐためのキャッシュ用変数
    std::vector<CCL::ECS::EntityID> m_rootEntities;
    std::vector<CCL::ECS::EntityID> m_allEntities;
    std::vector<FlatNode>           m_flatList;

    // 関数の宣言を以下のように World* を受け取るように変更
    void FlattenChildren(
        CCL::ECS::EntityID     firstChildID,
        int                    depth,
        CCL::ECS::World       *world,
        std::vector<FlatNode> &outFlatList);

    void DrawFlatNode(
        const FlatNode &node,
        EditorContext  &context);

    // Shift+クリックによる範囲選択を処理する (m_flatList 上で anchor→clicked を選択)
    void SelectRangeTo(CCL::ECS::EntityID clicked, EditorContext &context);

    bool IsDescendantOf(
        CCL::ECS::EntityID targetEntity,
        CCL::ECS::EntityID ancestorEntity,
        CCL::ECS::World   *world); // ← ここも World に変更

    char m_searchBuffer[256] = {0};
};
