// CameraDollyCartComponent のシリアライズ/インスペクタ定義。
// ファイルダイアログからの軌道データ選択と、相対パス変換を提供する。

#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#include <string.h> // <cstring>よりもC++ではこれか標準文字列を推奨

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

    static constexpr const char *displayName       = "Camera Dolly Cart (Rail)";
    static constexpr const char *category          = CCL::Categories::Camera; // カテゴリを明示
    static constexpr int         sortPriority      = 320;                     // カメラ群の並び順
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraDollyCartComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        bool changed = false;

        ImGui::SeparatorText("軌道アセット設定");

        char pathBuf[256];
        strncpy_s(pathBuf, comp.pathAsset.c_str(), sizeof(pathBuf));
        if (ImGui::InputText("軌道ファイル", pathBuf, sizeof(pathBuf)))
        {
            comp.pathAsset = pathBuf;
            comp.loadedPath.reset();
            comp.hasLoadError = false;
            changed           = true;
        }

        if (ImGui::Button("参照..."))
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

        if (ImGui::Button("新規作成..."))
        {
            if (auto pathOpt = FileDialog::SavePreset("CameraPath", "カメラ軌道を新規作成"))
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

        ImGui::SeparatorText("再生設定");

        changed |= ImGui::DragFloat("現在の進行距離(m)", &comp.currentDistance, 0.1f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("移動速度(m/s)", &comp.moveSpeed, 0.1f, 0.0f, 100.0f);
        changed |= ImGui::Checkbox("進行方向を向く", &comp.lookForward);

        if (comp.lookForward)
        {
            changed |= ImGui::DragFloat("先読みオフセット(m)", &comp.lookAheadOffset, 0.1f, 0.01f, 10.0f);
        }

        return changed;
    }
#else
    static bool CustomGui(CameraDollyCartComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(pathAsset, "軌道アセット", "Settings")
                CFIELD(currentDistance, "現在の進行距離", "Playback")
                    CFIELD(moveSpeed, "移動速度", "Playback")
                        CFIELD(lookForward, "進行方向を向く", "Playback")
                            CFIELD(lookAheadOffset, "先読みオフセット", "Playback")};
        return fields;
    }
};

REGISTER_COMPONENT(CameraDollyCartComponent, "CameraDollyCart")
REGISTER_COMPONENT_EDITOR_GUI(CameraDollyCartComponent)