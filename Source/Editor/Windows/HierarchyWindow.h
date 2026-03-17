#pragma once
#include "Editor/Core/EditorWindow.h"
#include <unordered_map>
#include <vector>

class HierarchyWindow : public EditorWindow {
  public:
    HierarchyWindow();

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    // isFlatSearch: 検索中など、階層を無視してリスト表示する際のフラグ
    void DrawEntityNode(CCL::ECS::EntityID                                             entityID,
        EditorContext                                                                 &context,
        const std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> &childrenMap,
        bool isFlatSearch = false);

    // 1万体の中から目的のエンティティを探すための検索バッファ
    char _searchBuffer[256] = {0};
};