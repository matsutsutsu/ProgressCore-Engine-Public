/// @file  CameraDollyCartComponentMeta.cpp
/// @brief CameraDollyCartComponent のシリアライズ/インスペクタ定義。
///        ファイルダイアログからの軌道データ選択と、相対パス変換を提供する。

#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include <cstring>

#ifdef ENABLE_EDITOR
#include "Engine/Platform/Core/Dialog.h"     // 独自ダイアログAPIの想定パス
#include "Engine/Platform/Core/FileSystem.h" // 仮想パス処理の想定パス
#endif

template <>
struct ComponentMeta<CameraDollyCartComponent>
{
    CCL_META_USING_THIS(CameraDollyCartComponent)
    static constexpr const char *displayName    = "Camera Dolly Cart (Rail)";
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraDollyCartComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        bool changed = false;

        ImGui::SeparatorText("Path Asset Settings");

        // パス文字列の編集 (バッファ渡し)
        char pathBuf[256];
        strncpy_s(pathBuf, comp.pathAsset.c_str(), sizeof(pathBuf));
        if (ImGui::InputText("Path File", pathBuf, sizeof(pathBuf)))
        {
            comp.pathAsset = pathBuf;
            comp.loadedPath.reset(); // パスが手動で書き換えられたら再ロードを促す
            changed = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
        {
            // 指示書厳守: OSダイアログを開き、絶対パスを相対パスに変換して保存する
            if (auto pathOpt = Engine::FileDialog::OpenPreset("CameraPath"))
            {
                comp.pathAsset = Engine::FileDialog::ConvertToRelativePath(*pathOpt);
                comp.loadedPath.reset(); // 違うファイルが選ばれたらキャッシュを破棄
                changed = true;
            }
        }

        ImGui::SeparatorText("Playback Settings");

        // ★修正: currentT -> currentDistance に変更し、UIのラベルも(m)等にして直感的にする
        changed |= ImGui::DragFloat("Current Dist (m)", &comp.currentDistance, 0.1f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Move Speed (m/s)", &comp.moveSpeed, 0.1f, 0.0f, 100.0f);
        changed |= ImGui::Checkbox("Look Forward", &comp.lookForward);

        if (comp.lookForward)
        {
            changed |= ImGui::DragFloat("Look Ahead Offset (m)", &comp.lookAheadOffset, 0.1f, 0.01f, 10.0f);
        }

        return changed; // Undo連携のために必須
    }
#else
    static bool CustomGui(CameraDollyCartComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    // 自動シリアライズ (JSONへのセーブ/ロード用)
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(pathAsset, "Path Asset", "Settings")

            // ★修正: currentT -> currentDistance に変更
            CFIELD(currentDistance, "Current Distance", "Playback")
                CFIELD(moveSpeed, "Move Speed", "Playback")
                    CFIELD(lookForward, "Look Forward", "Playback")
                        CFIELD(lookAheadOffset, "Look Ahead Offset", "Playback")};
        return fields;
    }
};

REGISTER_COMPONENT(CameraDollyCartComponent, "CameraDollyCart")
REGISTER_COMPONENT_EDITOR_GUI(CameraDollyCartComponent)