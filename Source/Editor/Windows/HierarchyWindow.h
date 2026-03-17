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
    void DrawEntityNode(CCL::ECS::EntityID                                             entityID,
        EditorContext                                                                 &context,
        const std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> &childrenMap);
};