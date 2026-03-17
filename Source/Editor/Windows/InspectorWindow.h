#pragma once
#include "Editor/Core/EditorWindow.h"

class InspectorWindow : public EditorWindow {
  public:
    InspectorWindow();

  protected:
    void DrawContents(EditorContext &context) override;
};