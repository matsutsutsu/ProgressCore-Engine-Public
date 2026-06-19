/// @file  StateTreeComponentMeta.cpp
/// @brief StateTreeComponent のシリアライズ・インスペクタ定義。
///
/// パターン B (カスタム GUI) を採用:
///   - assetHandle はパス文字列として保存したいので Fields() で assetHandle.value を
///     直接保存するのではなく、カスタムシリアライズ (パターン C への移行も視野) とする。
///   - M1 時点では最小限の GUI のみ。エディタ強化は M5 フェーズで行う。
///
/// ★ activeStates / scratch / activeDepth / needsReselect は
///   「実行時の一時データ」なので Fields() に含めない（保存対象外）。
#include "StateTreeComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include <imgui.h>
#endif

template<>
struct ComponentMeta<StateTreeComponent>
{
    CCL_META_USING_THIS(StateTreeComponent)

    static constexpr const char* displayName   = "State Tree";
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(StateTreeComponent& comp,
                          unsigned long long entityID = 0, void* world = nullptr)
    {
        bool changed = false;

        // --- アセットハンドル ---
        ImGui::Text("Asset Handle: %u", comp.assetHandle.value);

        // --- 現在の状態パス（実行時デバッグ表示）---
        if (comp.activeDepth > 0)
        {
            ImGui::Text("Active Depth: %u", (unsigned)comp.activeDepth);
            for (int i = 0; i < comp.activeDepth; ++i)
            {
                ImGui::Text("  [%d] state %u", i, (unsigned)comp.activeStates[i]);
            }
        }
        else
        {
            ImGui::TextDisabled("No active state");
        }

        // --- scratch 使用量 ---
        ImGui::Text("Scratch: %d bytes capacity", StateTreeComponent::SCRATCH_SIZE);

        // --- 強制リセットボタン（デバッグ用）---
        if (ImGui::Button("Force Reselect"))
        {
            comp.needsReselect = true;
            comp.activeDepth   = 0;
            comp.ClearScratch();
            changed = true;
        }

        return changed;
    }
#else
    static bool CustomGui(StateTreeComponent&, unsigned long long, void*) { return false; }
#endif

    // 保存対象フィールド:
    //   assetHandle.value のみ（uint32_t として保存・ロード）。
    //   実行時状態（activeStates, scratch など）は保存しない。
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(assetHandle.value, "Asset Handle", "StateTree")
        };
        return fields;
    }
};

REGISTER_COMPONENT(StateTreeComponent, "StateTree")
REGISTER_COMPONENT_EDITOR_GUI(StateTreeComponent)
