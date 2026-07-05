/// @file  CameraDollyCartComponentMeta.cpp
/// @brief CameraDollyCartComponent のシリアライズ/インスペクタ定義。
///        ファイルダイアログからの軌道データ選択と、相対パス変換を提供する。

#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#include <string.h> // ★修正: <cstring>よりもC++ではこれか標準文字列を推奨

#ifdef ENABLE_EDITOR
#include "Engine/Platform/Core/Dialog.h"     
#include "Engine/Platform/Core/FileSystem.h" 
#include <json.hpp>
#include <imgui.h>
#endif

template <>
struct ComponentMeta<CameraDollyCartComponent>
{
    CCL_META_USING_THIS(CameraDollyCartComponent)

    static constexpr const char *displayName    = "Camera Dolly Cart (Rail)";
    static constexpr const char *category       = CCL::Categories::Camera; // ★修正: カテゴリを明示
    static constexpr int         sortPriority   = 320; // ★追加: カメラ群の並び順
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraDollyCartComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        bool changed = false;

        ImGui::SeparatorText("Path Asset Settings");

        char pathBuf[256];
        strncpy_s(pathBuf, comp.pathAsset.c_str(), sizeof(pathBuf));
        if (ImGui::InputText("Path File", pathBuf, sizeof(pathBuf)))
        {
            comp.pathAsset = pathBuf;
            comp.loadedPath.reset();
            comp.hasLoadError = false; 
            changed           = true;
        }

        if (ImGui::Button("Browse..."))
        {
            if (auto pathOpt = FileDialog::OpenPreset("CameraPath"))
            {
                comp.pathAsset = FileDialog::ConvertToRelativePath(*pathOpt);
                comp.loadedPath.reset();
                comp.hasLoadError = false;
                changed           = true;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Create New..."))
        {
            if (auto pathOpt = FileDialog::SavePreset("CameraPath", "Create New Camera Path"))
            {
                std::string relPath = FileDialog::ConvertToRelativePath(*pathOpt);

                nlohmann::json j;
                j["points"] = nlohmann::json::array();
                j["points"].push_back({{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}});
                j["points"].push_back({{"x", 0.0f}, {"y", 0.0f}, {"z", 5.0f}}); 

                FileSystem::WriteAllText(relPath, j.dump(4));

                comp.pathAsset = relPath;
                comp.loadedPath.reset();
                comp.hasLoadError = false;
                changed           = true;
            }
        }

        ImGui::SeparatorText("Playback Settings");

        changed |= ImGui::DragFloat("Current Dist (m)", &comp.currentDistance, 0.1f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Move Speed (m/s)", &comp.moveSpeed, 0.1f, 0.0f, 100.0f);
        changed |= ImGui::Checkbox("Look Forward", &comp.lookForward);

        if (comp.lookForward)
        {
            changed |= ImGui::DragFloat("Look Ahead Offset (m)", &comp.lookAheadOffset, 0.1f, 0.01f, 10.0f);
        }

        return changed; 
    }
#else
    static bool CustomGui(CameraDollyCartComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(pathAsset, "Path Asset", "Settings")
            CFIELD(currentDistance, "Current Distance", "Playback")
            CFIELD(moveSpeed, "Move Speed", "Playback")
            CFIELD(lookForward, "Look Forward", "Playback")
            CFIELD(lookAheadOffset, "Look Ahead Offset", "Playback")
        };
        return fields;
    }
};

REGISTER_COMPONENT(CameraDollyCartComponent, "CameraDollyCart")
REGISTER_COMPONENT_EDITOR_GUI(CameraDollyCartComponent)