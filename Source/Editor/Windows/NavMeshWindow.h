#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Editor/Utils/NavMeshBuilder.h"

class NavMeshWindow : public EditorWindow
{
public:
    NavMeshWindow();

protected:
    void DrawContents(EditorContext &context) override;

private:
    // ウィンドウ自体が設定の記憶を持つ（staticの排除）
    NavMeshBuildSettings m_navSettings;
};