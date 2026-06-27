/// @file  CameraDollyCartComponentMeta.cpp
/// @brief CameraDollyCartComponent のシリアライズ/インスペクタ定義。
///        ファイルダイアログからの軌道データ選択と、相対パス変換を提供する。

#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include <cstring>

#ifdef ENABLE_EDITOR
#include "Engine/Platform/Core/Dialog.h"     // 独自ダイアログAPIの想定パス
#include "Engine/Platform/Core/FileSystem.h" // 仮想パス処理の想定パス
#include <json.hpp>
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
            comp.loadedPath.reset();
            comp.hasLoadError = false; // ★追加: 手入力でパスが変わったらエラーフラグを解除して再試行を許可する
            changed           = true;
        }

        // --- 既存の Browse ボタン ---
        if (ImGui::Button("Browse..."))
        {
            if (auto pathOpt = FileDialog::OpenPreset("CameraPath"))
            {
                comp.pathAsset = FileDialog::ConvertToRelativePath(*pathOpt);
                comp.loadedPath.reset();
                comp.hasLoadError = false; // ★追加: エラー解除
                changed           = true;
            }
        }

        ImGui::SameLine();

        // --- New ボタン (新規作成ワークフロー) ---
        if (ImGui::Button("Create New..."))
        {
            // OSの保存ダイアログを開く
            if (auto pathOpt = FileDialog::SavePreset("CameraPath", "Create New Camera Path"))
            {
                std::string relPath = FileDialog::ConvertToRelativePath(*pathOpt);

                // 初期の軌道データ（2つのウェイポイントを持つ最低限のデータ）を作成
                nlohmann::json j;
                j["points"] = nlohmann::json::array();
                j["points"].push_back({{"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}});
                j["points"].push_back({{"x", 0.0f}, {"y", 0.0f}, {"z", 5.0f}}); // 5m先の点

                // 物理ファイルとしてディスクに書き出す
                FileSystem::WriteAllText(relPath, j.dump(4));

                // コンポーネントに自動セットアップ
                comp.pathAsset = relPath;
                comp.loadedPath.reset();
                comp.hasLoadError = false; // エラー解除
                changed           = true;
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