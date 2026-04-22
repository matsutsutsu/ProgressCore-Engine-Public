#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/GamePlay/Animation/Data/AnimStateMachine.h"
#include <string>

// imgui-node-editor の前方宣言
namespace ax { namespace NodeEditor { struct EditorContext; } }

class AnimGraphWindow : public EditorWindow {
public:
    AnimGraphWindow();
    virtual ~AnimGraphWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    void DrawToolbar();
    void DrawNodeWorkspace();
    void DrawInspector();

    void LoadGraph(const std::string& path);
    void SaveGraph(const std::string& path);

private:
    AnimStateGraph _currentGraph;
    std::string _currentFilePath = "Assets/Animations/PlayerAnimGraph.json";

    // imgui-node-editor 専用コンテキスト
    ax::NodeEditor::EditorContext* _nodeContext = nullptr;

    uint32_t _selectedNodeHash = 0;
    char _newStateName[64] = "";
};