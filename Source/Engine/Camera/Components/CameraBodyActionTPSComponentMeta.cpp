/// @file CameraBodyActionTPSComponentMeta.cpp
/// @brief CameraBodyActionTPS コンポーネントのシリアライズおよびインスペクタGUIマクロ定義。

#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

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
    static constexpr const char *category       = CCL::Categories::Camera;
    static constexpr int         sortPriority   = 350;
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
                    // ペイロードは HierarchyWindow が sizeof(CCL::ECS::EntityID) で積んでいる。
                    // 読み側で生の型を書くと、EntityID の幅を変えたときにここだけ食い違う。
                    link.id    = *(const CCL::ECS::EntityID *)payload->Data;
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
        changed |= ImGui::DragFloat(u8"注視ずれ上限 (lockOnMaxLookShift)", &comp.lockOnMaxLookShift, 0.1f, 0.0f, 15.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"注視点が敵側へずれる上限(m)。小さいほどプレイヤーが常に画面内。");
        changed |= ImGui::DragFloat(u8"離間による引き量 (lockOnDistanceGain)", &comp.lockOnDistanceGain, 0.01f, 0.0f, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"敵と離れるほどカメラを引く量(m/m)。両者を画面に収める。0で引き無し。");
        changed |= ImGui::DragFloat(u8"引き量の上限 (lockOnMaxExtraDistance)", &comp.lockOnMaxExtraDistance, 0.1f, 0.0f, 30.0f);

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
        changed |= ImGui::DragFloat(u8"パッドX軸回転速度 (lookSpeedX)", &comp.lookSpeedX, 1.0f, 0.0f, 500.0f);
        changed |= ImGui::DragFloat(u8"パッドY軸回転速度 (lookSpeedY)", &comp.lookSpeedY, 1.0f, 0.0f, 500.0f);

        ImGui::SeparatorText(u8"マウス視点 (Mouse Look)");
        changed |= ImGui::Checkbox(u8"右クリック押下中のみ (mouseLookRequiresButton)", &comp.mouseLookRequiresButton);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"ON=右ドラッグ中だけ視点回転(エディタ向き)。OFF=常時マウス視点(フルスクリーン向き)。");
        changed |= ImGui::DragFloat(u8"マウス水平感度 (mouseSensitivityX)", &comp.mouseSensitivityX, 0.01f, 0.0f, 2.0f);
        changed |= ImGui::DragFloat(u8"マウス垂直感度 (mouseSensitivityY)", &comp.mouseSensitivityY, 0.01f, 0.0f, 2.0f);
        changed |= ImGui::Checkbox(u8"上下反転 (mouseInvertY)", &comp.mouseInvertY);
        changed |= ImGui::Checkbox(u8"カーソル中央固定 (enableCursorLock)", &comp.enableCursorLock);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"マウスルック中、カーソルを画面中央へ固定＋非表示にし、画面端で視点が止まらないようにする。ポーズ/非フォーカスで自動解除。");

        ImGui::SeparatorText(u8"滑らかさ (Smoothing)");
        changed |= ImGui::DragFloat(u8"位置追従遅延 (positionDamping)", &comp.positionDamping, 0.1f, 0.0f, 50.0f);
        changed |= ImGui::DragFloat(u8"視野角補間速度 (fovDamping)", &comp.fovDamping, 0.1f, 0.0f, 50.0f);

        ImGui::SeparatorText(u8"カメラ衝突 (Collision)");
        changed |= ImGui::Checkbox(u8"衝突回避 有効 (collisionEnabled)", &comp.collisionEnabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"地形/建物にカメラがめり込まないよう、手前へ寄せる。");
        changed |= ImGui::DragFloat(u8"衝突余白/半径 (collisionRadius)", &comp.collisionRadius, 0.01f, 0.0f, 2.0f);
        changed |= ImGui::DragFloat(u8"最小距離 (collisionMinDistance)", &comp.collisionMinDistance, 0.05f, 0.1f, 10.0f);
        changed |= ImGui::DragFloat(u8"寄せる速さ (collisionInDamping)", &comp.collisionInDamping, 0.5f, 1.0f, 100.0f);
        changed |= ImGui::DragFloat(u8"戻す速さ (collisionOutDamping)", &comp.collisionOutDamping, 0.5f, 1.0f, 100.0f);
        changed |= ImGui::DragFloat(u8"寄った時の頭寄せ (collisionHeadBias)", &comp.collisionHeadBias, 0.01f, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"カメラが寄った時に足元ではなく頭付近の高さへ持ち上げる強さ(0=そのまま,1=頭の高さ)。");

        ImGui::SeparatorText(u8"ルックアヘッド (Lookahead)");
        changed |= ImGui::DragFloat(u8"前方注視係数 (lookaheadMultiplier)", &comp.lookaheadMultiplier, 0.05f, 0.0f, 5.0f);
        changed |= ImGui::DragFloat(u8"注視遅延 (lookaheadDamping)", &comp.lookaheadDamping, 0.1f, 0.0f, 50.0f);

        return changed;
    }
#else
    // 構造体名(CameraBodyTPSComponent)と完全に一致させること！
    static bool CustomGui(CameraBodyTPSComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "追従ターゲット", "Settings")
            CFIELD(lockOnTarget, "ロックオン対象", "Settings")
            CFIELD(isLockOnActive, "ロックオン有効化", "LockOn")
            CFIELD_FLOAT_RANGED(lockOnPitch, "ロックオン固定ピッチ", 0.5f, -60.0f, 60.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnSubjugationRatio, "敵への注視偏重率", 0.01f, 0.0f, 0.5f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnRotationDamping, "自動回り込み速度", 0.1f, 1.0f, 20.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnMaxLookShift, "注視ずれ上限", 0.1f, 0.0f, 15.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnDistanceGain, "離間による引き量", 0.01f, 0.0f, 2.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnMaxExtraDistance, "引き量の上限", 0.1f, 0.0f, 30.0f, "LockOn")

            CFIELD_FLOAT3_FAST(targetOffset, "注視点オフセット", 0.1f, "Positioning")
            CFIELD_FLOAT_RANGED(distance, "カメラ距離", 0.1f, 1.0f, 100.0f, "Positioning")
            CFIELD_FLOAT_RANGED(baseFov, "基本視野角", 1.0f, 10.0f, 120.0f, "Dynamic Lens")
            CFIELD_FLOAT_RANGED(dashFov, "ダッシュ視野角", 1.0f, 10.0f, 160.0f, "Dynamic Lens")
            CFIELD_FLOAT_RANGED(currentPitch, "初期ピッチ角", 1.0f, -85.0f, 85.0f, "Orbit Settings")
            CFIELD_FLOAT_RANGED(currentYaw, "初期ヨー角", 1.0f, -360.0f, 360.0f, "Orbit Settings")
            CFIELD_FLOAT_RANGED(minPitch, "最小ピッチ制限", 1.0f, -89.0f, 89.0f, "Orbit Constraints")
            CFIELD_FLOAT_RANGED(maxPitch, "最大ピッチ制限", 1.0f, -89.0f, 89.0f, "Orbit Constraints")
            CFIELD_FLOAT_RANGED(lookSpeedX, "回転速度X", 1.0f, 0.0f, 500.0f, "Control Settings")
            CFIELD_FLOAT_RANGED(lookSpeedY, "回転速度Y", 1.0f, 0.0f, 500.0f, "Control Settings")
            CFIELD(mouseLookRequiresButton, "右クリック中のみ視点回転", "Mouse Look")
            CFIELD_FLOAT_RANGED(mouseSensitivityX, "マウス水平感度", 0.01f, 0.0f, 2.0f, "Mouse Look")
            CFIELD_FLOAT_RANGED(mouseSensitivityY, "マウス垂直感度", 0.01f, 0.0f, 2.0f, "Mouse Look")
            CFIELD(mouseInvertY, "上下反転", "Mouse Look")
            CFIELD(enableCursorLock, "カーソル中央固定", "Mouse Look")
            CFIELD_FLOAT_RANGED(positionDamping, "位置追従遅延", 0.1f, 0.0f, 50.0f, "Smoothing")
            CFIELD_FLOAT_RANGED(fovDamping, "視野角補間速度", 0.1f, 0.0f, 50.0f, "Smoothing")

            CFIELD(collisionEnabled, "衝突回避 有効", "Collision")
            CFIELD_FLOAT_RANGED(collisionRadius, "衝突余白/半径", 0.01f, 0.0f, 2.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionMinDistance, "最小距離", 0.05f, 0.1f, 10.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionInDamping, "寄せる速さ", 0.5f, 1.0f, 100.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionOutDamping, "戻す速さ", 0.5f, 1.0f, 100.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionHeadBias, "寄った時の頭寄せ", 0.01f, 0.0f, 1.0f, "Collision")
            CFIELD_FLOAT_RANGED(lookaheadMultiplier, "前方注視係数", 0.05f, 0.0f, 5.0f, "Lookahead")
            CFIELD_FLOAT_RANGED(lookaheadDamping, "注視遅延", 0.1f, 0.0f, 50.0f, "Lookahead")

            CFIELD_FLOAT3_FAST(currentLookahead, "現在の注視オフセット", 0.1f, "State")
        };
        return fields;
    }
};

// カテゴリを "Camera"（生文字）から "CameraBodyTPS" などの正式な識別子名に変更
REGISTER_COMPONENT(CameraBodyTPSComponent, "CameraBodyTPS")
REGISTER_COMPONENT_EDITOR_GUI(CameraBodyTPSComponent)