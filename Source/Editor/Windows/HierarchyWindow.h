#pragma once
#include "Editor/Core/EditorWindow.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class HierarchyWindow : public EditorWindow {
public:
    HierarchyWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    struct FlatNode {
        CCL::ECS::EntityID entityID;
        int depth;
        bool hasChildren;
    };

    std::unordered_set<CCL::ECS::EntityID> _openNodes;

    // ★追加: 毎フレームのメモリ確保を防ぐためのキャッシュ用変数
    std::vector<CCL::ECS::EntityID> _rootEntities;
    std::vector<CCL::ECS::EntityID> _allEntities;
    std::vector<FlatNode>           _flatList;

    // 関数の宣言を以下のように World* を受け取るように変更
    void FlattenChildren(
        CCL::ECS::EntityID firstChildID,
        int depth,
        CCL::ECS::World* world,
        std::vector<FlatNode>& outFlatList);

    void DrawFlatNode(
        const FlatNode& node,
        EditorContext& context);

    bool IsDescendantOf(
        CCL::ECS::EntityID targetEntity,
        CCL::ECS::EntityID ancestorEntity,
        CCL::ECS::World* world); // ← ここも World に変更

    char _searchBuffer[256] = { 0 };
};