#pragma once
#include "Editor/Core/EditorWindow.h"

class MainToolbarWindow : public EditorWindow {
public:
    MainToolbarWindow();

protected:
    void DrawContents(EditorContext& context) override;
};