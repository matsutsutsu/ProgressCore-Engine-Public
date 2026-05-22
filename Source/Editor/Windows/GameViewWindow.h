#pragma once
#include "Editor/Core/EditorWindow.h"

class GameViewWindow : public EditorWindow {
public:
    GameViewWindow() : EditorWindow("Game View") {}

protected:
    void DrawContents(EditorContext& context) override;
};