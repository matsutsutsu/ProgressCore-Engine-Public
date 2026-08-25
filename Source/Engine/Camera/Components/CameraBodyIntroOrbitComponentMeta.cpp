/// @file  CameraBodyIntroOrbitComponentMeta.cpp
/// @brief CameraBodyIntroOrbit のシリアライズ/インスペクタ定義。

#include "Engine/Camera/Components/CameraBodyIntroOrbitComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#include <string>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <>
struct ComponentMeta<CameraBodyIntroOrbit>
{
    CCL_META_USING_THIS(CameraBodyIntroOrbit)

    static constexpr const char *displayName       = "Camera Body (Intro Orbit)";
    static constexpr const char *category          = CCL::Categories::Camera;
    static constexpr int         sortPriority      = 360;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraBodyIntroOrbit &comp, unsigned long long /*entityID*/ = 0,
                          void * /*world*/ = nullptr)
    {
        bool changed = false;

        ImGui::SeparatorText(u8"注視対象");

        std::string btnLabel = (comp.target.id == 0)
                                   ? "Target: None (Drag Entity)"
                                   : "Target ID: " + std::to_string(comp.target.id);
        ImGui::Button(btnLabel.c_str(), ImVec2(-1, 0));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID"))
            {
                // ペイロードは HierarchyWindow が sizeof(CCL::ECS::EntityID) で積んでいる。
                comp.target.id = *(const CCL::ECS::EntityID *)payload->Data;
                changed        = true;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                           u8"[ ここに Hierarchy からボスをドロップ ]");
        changed |= ImGui::DragFloat3(u8"注視オフセット (targetOffset)", &comp.targetOffset.x, 0.1f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"足元ではなく胸〜頭を見るための持ち上げ量。");

        ImGui::SeparatorText(u8"軌道 (開始 → 終了)");
        ImGui::TextDisabled(u8"角度は TPS カメラと同じ規約。200→20 で半周回り込む。");
        changed |= ImGui::DragFloat(u8"開始 水平角(度)", &comp.startAngle, 1.0f, -720.0f, 720.0f);
        changed |= ImGui::DragFloat(u8"終了 水平角(度)", &comp.endAngle, 1.0f, -720.0f, 720.0f);
        changed |= ImGui::DragFloat(u8"開始 距離(m)", &comp.startRadius, 0.5f, 0.5f, 200.0f);
        changed |= ImGui::DragFloat(u8"終了 距離(m)", &comp.endRadius, 0.5f, 0.5f, 200.0f);
        changed |= ImGui::DragFloat(u8"開始 高さ(m)", &comp.startHeight, 0.2f, -50.0f, 100.0f);
        changed |= ImGui::DragFloat(u8"終了 高さ(m)", &comp.endHeight, 0.2f, -50.0f, 100.0f);

        ImGui::SeparatorText(u8"画角 (Dynamic Lens)");
        changed |= ImGui::DragFloat(u8"開始 画角(度)", &comp.startFov, 0.5f, 10.0f, 150.0f);
        changed |= ImGui::DragFloat(u8"終了 画角(度)", &comp.endFov, 0.5f, 10.0f, 150.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"終了を狭くすると望遠の圧縮効果でボスに圧が出る。");

        ImGui::SeparatorText(u8"再生");
        int easeIdx = static_cast<int>(comp.easeMode);
        if (ImGui::Combo(u8"補間カーブ (easeMode)", &easeIdx, kCameraEaseModeNames,
                         static_cast<int>(kCameraEaseModeCount)))
        {
            comp.easeMode = static_cast<CameraEaseMode>(easeIdx);
            changed       = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"Linear は等速で「監視カメラ」に見える。演出は EaseOut 推奨。");

        changed |= ImGui::DragFloat(u8"演出の長さ(秒)", &comp.duration, 0.1f, 0.1f, 60.0f);
        changed |= ImGui::DragFloat(u8"開始待ち(秒)", &comp.startDelay, 0.05f, 0.0f, 10.0f);
        changed |= ImGui::Checkbox(u8"シーン開始で自動再生 (playOnStart)", &comp.playOnStart);
        changed |= ImGui::Checkbox(u8"スキップ許可 (allowSkip)", &comp.allowSkip);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"攻撃/ジャンプでスキップ。展示会では ON 推奨。");

        ImGui::SeparatorText(u8"カメラ優先度");
        changed |= ImGui::DragInt(u8"再生中 (activePriority)", &comp.activePriority, 1, -1000, 1000);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"プレイヤー追従カメラ(200)より高くすること。");
        changed |= ImGui::DragInt(u8"待機中 (idlePriority)", &comp.idlePriority, 1, -1000, 1000);

        // --- 実行時状態の可視化とテスト再生 ---
        ImGui::SeparatorText(u8"状態 (実行時)");
        ImGui::Text(u8"playing=%s  finished=%s  elapsed=%.2f / %.2f",
                    comp.playing ? "yes" : "no", comp.finished ? "yes" : "no",
                    comp.elapsed, comp.duration);

        if (ImGui::Button(u8"最初から再生", ImVec2(140, 0)))
        {
            comp.playing       = true;
            comp.finished      = false;
            comp.skipped       = false;
            comp.previewing    = false;
            comp.elapsed       = 0.0f;
            comp.delayTimer    = 0.0f;
            comp.resolveTimer  = 0.0f;
            comp.autoPlayArmed = false;
            changed            = true;
        }
        ImGui::SameLine();
        // 進行度を手で掴んで絵作りする（プレビュー用スクラブ）。
        // Dolly には無かった機能で、これが無いと毎回再生し直すしかない。
        if (ImGui::SliderFloat(u8"スクラブ", &comp.elapsed, 0.0f, comp.duration, "%.2fs"))
        {
            // 掴んでいる間は自動進行を止め、previewing でカメラだけ掌握させる。
            // previewing を立てないと「再生していない＝出番待ち」と判定され、
            // 演出システムがカメラを譲ってしまい絵が見えない。
            comp.playing    = false;
            comp.finished   = false;
            comp.previewing = true;
            changed         = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"プレビュー終了"))
        {
            comp.previewing = false;
            comp.finished   = true; // 待機優先度へ落としてプレイヤーカメラへ戻す
            changed         = true;
        }

        return changed;
    }
#else
    static bool CustomGui(CameraBodyIntroOrbit &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        // playing / finished / skipped / elapsed / delayTimer / autoPlayArmed は
        // 毎フレーム書き換わる実行時状態なので、意図的にシリアライズしない。
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "注視対象", "Target")
            CFIELD_FLOAT3_FAST(targetOffset, "注視オフセット", 0.1f, "Target")

            CFIELD_FLOAT_RANGED(startAngle, "開始 水平角", 1.0f, -720.0f, 720.0f, "Orbit")
            CFIELD_FLOAT_RANGED(endAngle, "終了 水平角", 1.0f, -720.0f, 720.0f, "Orbit")
            CFIELD_FLOAT_RANGED(startRadius, "開始 距離", 0.5f, 0.5f, 200.0f, "Orbit")
            CFIELD_FLOAT_RANGED(endRadius, "終了 距離", 0.5f, 0.5f, 200.0f, "Orbit")
            CFIELD_FLOAT_RANGED(startHeight, "開始 高さ", 0.2f, -50.0f, 100.0f, "Orbit")
            CFIELD_FLOAT_RANGED(endHeight, "終了 高さ", 0.2f, -50.0f, 100.0f, "Orbit")

            CFIELD_FLOAT_RANGED(startFov, "開始 画角", 0.5f, 10.0f, 150.0f, "Lens")
            CFIELD_FLOAT_RANGED(endFov, "終了 画角", 0.5f, 10.0f, 150.0f, "Lens")

            CFIELD_ENUM_U8(easeMode, "補間カーブ", kCameraEaseModeNames, kCameraEaseModeCount, "Playback")
            CFIELD_FLOAT_RANGED(duration, "演出の長さ", 0.1f, 0.1f, 60.0f, "Playback")
            CFIELD_FLOAT_RANGED(startDelay, "開始待ち", 0.05f, 0.0f, 10.0f, "Playback")
            CFIELD(playOnStart, "シーン開始で自動再生", "Playback")
            CFIELD(allowSkip, "スキップ許可", "Playback")

            CFIELD_INT_RANGED(activePriority, "再生中の優先度", -1000, 1000, "Priority")
            CFIELD_INT_RANGED(idlePriority, "待機中の優先度", -1000, 1000, "Priority")
        };
        return fields;
    }
};

REGISTER_COMPONENT(CameraBodyIntroOrbit, "CameraBodyIntroOrbit")
REGISTER_COMPONENT_EDITOR_GUI(CameraBodyIntroOrbit)
