/// @file  StateTreeComponentMeta.cpp
/// @brief StateTreeComponent のシリアライズ / インスペクタ定義。
///
/// パターンC（カスタムシリアライズ）を採用:
///   Save : assetName だけ JSON に書き出す
///   Load : assetName を読み込むだけ（Handle 解決は LoadContext に world がないため不可）
///
/// Handle 解決のタイミング:
///   LoadContext は LocalIndex ↔ EntityID の変換しか持たず、World にアクセスできない。
///   そのため assetName → Handle の解決は StateTreeResolveSystem（L01_Input）が担う。
///   「Load でアセット名だけ復元 → ResolveSystem が最初のフレームで Handle をセット」
///   という 2 段階になる。

#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Platform/Core/Logger.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include <imgui.h>
#endif


// ================================================================
// パターンC: カスタムシリアライズ
// ================================================================
namespace ComponentMetaJson
{
    template <>
    struct ComponentSerializer<StateTreeComponent>
    {
        // --- Save: assetName だけ書き出す ---
        static void Save(json& j, const StateTreeComponent& c, SaveContext* /*ctx*/)
        {
            j["assetName"] = c.assetName;
            // assetHandle / activeStates / scratch は実行時専用なので保存しない
        }

        // --- Load: assetName を読むだけ。Handle 解決は ResolveSystem に任せる ---
        static void Load(const json& j, StateTreeComponent& c, LoadContext* /*ctx*/)
        {
            if (j.contains("assetName"))
                c.assetName = j["assetName"].get<std::string>();

            // assetHandle は初期値（無効）のまま。
            // StateTreeResolveSystem が L01_Input で assetName → Handle を解決する。
        }
    };
} // namespace ComponentMetaJson


// ================================================================
// ComponentMeta<T> 本体
// ================================================================
template <> struct ComponentMeta<StateTreeComponent>
{
    CCL_META_USING_THIS(StateTreeComponent)

        static constexpr const char* displayName = "StateTree";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(StateTreeComponent& comp,
        unsigned long long /*entityID*/ = 0,
        void* /*world*/ = nullptr)
    {
        bool changed = false;

        // --- アセット名の直接入力 ---
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", comp.assetName.c_str());
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("Asset Name", buf, sizeof(buf)))
        {
            comp.assetName = buf;
            // 名前が変わったら Handle を無効化して再解決させる
            comp.assetHandle = CCL::AI::StateTreeHandle{};
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(例: Enemy / Boss)");

        // --- Handle の状態表示 ---
        if (comp.assetHandle.IsValid())
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f },
                "Handle: %u  OK", comp.assetHandle.value);
        else
            ImGui::TextColored({ 1.0f, 0.5f, 0.3f, 1.0f },
                "Handle: 未解決  (ResolveSystem が次フレームでセット)");

        // --- アクティブ状態の表示（実行中デバッグ用）---
        ImGui::Separator();
        if (comp.activeDepth > 0)
        {
            ImGui::TextDisabled("Active States:");
            for (int d = 0; d < (int)comp.activeDepth; ++d)
                ImGui::Text("  [%d] states[%u]", d, (unsigned)comp.activeStates[d]);
        }
        else
        {
            ImGui::TextDisabled("Active: (none)");
        }

        return changed;
    }
#else
    static bool CustomGui(StateTreeComponent&, unsigned long long, void*) { return false; }
#endif

    // パターンC なので Fields() は空
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(StateTreeComponent, "StateTree")
REGISTER_COMPONENT_EDITOR_GUI(StateTreeComponent)