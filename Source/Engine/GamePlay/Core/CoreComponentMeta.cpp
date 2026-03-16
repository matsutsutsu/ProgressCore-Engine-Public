#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/GamePlay/Core/NameComponent.h"

#include "Engine/Serialization/ComponentSerializers.h"

template <> struct ComponentMeta<NameComponent> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Name";

    //  char配列を安全に編集するため完全カスタムGUI
    static constexpr bool hasCustomGui = true;

    static bool CustomGui(NameComponent &comp)
    {
        bool changed = false;

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Entity Name");
        // char[32] のバッファを直接ImGuiに渡して編集させる
        if (ImGui::InputText("##NameInput", comp.name, sizeof(comp.name))) {
            changed = true;
        }
        return changed;
    }

    // char配列の自動シリアライズは現在のマクロ(std::string用)では危険なため、空にしておく。
    // ※後日、ComponentMetaJson 側に char配列専用の特殊化を追加することを推奨します。
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};


void RegisterCoreMeta()
{
    // ※ JSON登録はシリアライズ対応後にアンコメント
     ComponentMetaJson::RegisterMetaT<NameComponent>(ComponentRegistry::Instance(),"Name");
    ComponentMetaImGui::RegisterGuiMeta<NameComponent>(ComponentGuiRegistry::Instance());
}
