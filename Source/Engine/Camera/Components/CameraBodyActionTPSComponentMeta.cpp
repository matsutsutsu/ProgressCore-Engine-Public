/// @file CameraBodyActionTPSComponentMeta.cpp
/// @brief CameraBodyActionTPS コンポーネントのシリアライズおよびインスペクタGUIマクロ定義。

#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#include <string>
#include <algorithm> 

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// CameraBodyActionTPS メタデータ定義
// ============================================================================
template <>
struct ComponentMeta<CameraBodyTPSComponent>
{
    CCL_META_USING_THIS(CameraBodyTPSComponent)

    static constexpr const char *displayName    = "Camera Body Action TPS";
    static constexpr const char *category       = CCL::Categories::Camera; // ★追加
    static constexpr int         sortPriority   = 350; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraBodyTPSComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        bool changed = false;

        ImGui::SeparatorText(u8"追従・注視ターゲット設定");

        auto RenderEntityLinkBox = [](const char *label, CCL::Serialization::EntityLink &link) -> bool {
            bool        dndChanged = false;
            std::string btnLabel   = (link.id == 0) ? std::string(label) + ": None (Drag Entity)" : std::string(label) + " ID: " + std::to_string(link.id);
            ImGui::Button(btnLabel.c_str(), ImVec2(-1, 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID"))
                {
                    link.id    = *(const unsigned long long *)payload->Data;
                    dndChanged = true;
                }
                ImGui::EndDragDropTarget();
            }
            return dndChanged;
        };

        changed |= RenderEntityLinkBox("Player Target", comp.target);
        changed |= RenderEntityLinkBox("LockOn Enemy", comp.lockOnTarget);

        ImGui::SeparatorText(u8"ロックオン設定 (Lock-on Settings)");
        changed |= ImGui::Checkbox(u8"ロックオン有効化 (isLockOnActive)", &comp.isLockOnActive);
        changed |= ImGui::DragFloat(u8"ロックオン固定ピッチ (lockOnPitch)", &comp.lockOnPitch, 0.5f, -60.0f, 60.0f);
        changed |= ImGui::DragFloat(u8"敵への注視偏重率 (lockOnSubjugationRatio)", &comp.lockOnSubjugationRatio, 0.01f, 0.0f, 0.5f);
        changed |= ImGui::DragFloat(u8"自動回り込み速度 (lockOnRotationDamping)", &comp.lockOnRotationDamping, 0.1f, 1.0f, 20.0f);

        ImGui::SeparatorText(u8"位置・追従 (Positioning)");
        changed |= ImGui::DragFloat3(u8"注視点オフセット (targetOffset)", &comp.targetOffset.x, 0.1f);
        changed |= ImGui::DragFloat(u8"カメラ距離 (distance)", &comp.distance, 0.1f, 1.0f, 100.0f);

        ImGui::SeparatorText(u8"視野角・レンズ (Dynamic Lens)");
        changed |= ImGui::DragFloat(u8"基本視野角 (baseFov)", &comp.baseFov, 0.1f, 10.0f, 120.0f);
        changed |= ImGui::DragFloat(u8"ダッシュ視野角 (dashFov)", &comp.dashFov, 0.1f, 10.0f, 160.0f);

        ImGui::SeparatorText(u8"回転・オービット (Orbit Settings)");
        changed |= ImGui::DragFloat(u8"初期ピッチ角 (currentPitch)", &comp.currentPitch, 0.5f, -85.0f, 85.0f);
        changed |= ImGui::DragFloat(u8"初期ヨー角 (currentYaw)", &comp.currentYaw, 0.5f, -360.0f, 360.0f);

        ImGui::SeparatorText(u8"回転制限 (Orbit Constraints)");
        changed |= ImGui::DragFloat(u8"最小ピッチ制限 (minPitch)", &comp.minPitch, 0.5f, -89.0f, 89.0f);
        changed |= ImGui::DragFloat(u8"最大ピッチ制限 (maxPitch)", &comp.maxPitch, 0.5f, -89.0f, 89.0f);

        ImGui::SeparatorText(u8"操作感度 (Control Settings)");
        changed |= ImGui::DragFloat(u8"X軸回転速度 (lookSpeedX)", &comp.lookSpeedX, 1.0f, 0.0f, 500.0f);
        changed |= ImGui::DragFloat(u8"Y軸回転速度 (lookSpeedY)", &comp.lookSpeedY, 1.0f, 0.0f, 500.0f);

        ImGui::SeparatorText(u8"滑らかさ (Smoothing)");
        changed |= ImGui::DragFloat(u8"位置追従遅延 (positionDamping)", &comp.positionDamping, 0.1f, 0.0f, 50.0f);
        changed |= ImGui::DragFloat(u8"視野角補間速度 (fovDamping)", &comp.fovDamping, 0.1f, 0.0f, 50.0f);

        ImGui::SeparatorText(u8"ルックアヘッド (Lookahead)");
        changed |= ImGui::DragFloat(u8"前方注視係数 (lookaheadMultiplier)", &comp.lookaheadMultiplier, 0.05f, 0.0f, 5.0f);
        changed |= ImGui::DragFloat(u8"注視遅延 (lookaheadDamping)", &comp.lookaheadDamping, 0.1f, 0.0f, 50.0f);

        return changed;
    }
#else
    // ★ 修正: 構造体名(CameraBodyTPSComponent)と完全に一致させること！
    static bool CustomGui(CameraBodyTPSComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "Target", "Settings")
            CFIELD(lockOnTarget, "LockOn Target", "Settings")
            CFIELD(isLockOnActive, u8"ロックオン有効化 (isLockOnActive)", "LockOn")
            CFIELD_FLOAT_RANGED(lockOnPitch, u8"ロックオン固定ピッチ (lockOnPitch)", 0.5f, -60.0f, 60.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnSubjugationRatio, u8"敵への注視偏重率 (lockOnSubjugationRatio)", 0.01f, 0.0f, 0.5f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnRotationDamping, u8"自動回り込み速度 (lockOnRotationDamping)", 0.1f, 1.0f, 20.0f, "LockOn")

            CFIELD_FLOAT3_FAST(targetOffset, "Target Offset", 0.1f, "Positioning")
            CFIELD_FLOAT_RANGED(distance, "Distance", 0.1f, 1.0f, 100.0f, "Positioning")
            CFIELD_FLOAT_RANGED(baseFov, "Base FOV", 1.0f, 10.0f, 120.0f, "Dynamic Lens")
            CFIELD_FLOAT_RANGED(dashFov, "Dash FOV", 1.0f, 10.0f, 160.0f, "Dynamic Lens")
            CFIELD_FLOAT_RANGED(currentPitch, "Initial Pitch", 1.0f, -85.0f, 85.0f, "Orbit Settings")
            CFIELD_FLOAT_RANGED(currentYaw, "Initial Yaw", 1.0f, -360.0f, 360.0f, "Orbit Settings")
            CFIELD_FLOAT_RANGED(minPitch, "Min Pitch", 1.0f, -89.0f, 89.0f, "Orbit Constraints")
            CFIELD_FLOAT_RANGED(maxPitch, "Max Pitch", 1.0f, -89.0f, 89.0f, "Orbit Constraints")
            CFIELD_FLOAT_RANGED(lookSpeedX, "Look Speed X", 1.0f, 0.0f, 500.0f, "Control Settings")
            CFIELD_FLOAT_RANGED(lookSpeedY, "Look Speed Y", 1.0f, 0.0f, 500.0f, "Control Settings")
            CFIELD_FLOAT_RANGED(positionDamping, "Pos Damping", 0.1f, 0.0f, 50.0f, "Smoothing")
            CFIELD_FLOAT_RANGED(fovDamping, "FOV Damping", 0.1f, 0.0f, 50.0f, "Smoothing")
            CFIELD_FLOAT_RANGED(lookaheadMultiplier, u8"前方注視係数 (lookaheadMultiplier)", 0.05f, 0.0f, 5.0f, "Lookahead")
            CFIELD_FLOAT_RANGED(lookaheadDamping, u8"注視遅延 (lookaheadDamping)", 0.1f, 0.0f, 50.0f, "Lookahead")
            
            CFIELD_FLOAT3_FAST(currentLookahead, u8"現在の注視オフセット (currentLookahead)", 0.1f, "State")
        };
        return fields;
    }
};

// ★修正: カテゴリを "Camera"（生文字）から "CameraBodyTPS" などの正式な識別子名に変更
REGISTER_COMPONENT(CameraBodyTPSComponent, "CameraBodyTPS")
REGISTER_COMPONENT_EDITOR_GUI(CameraBodyTPSComponent)