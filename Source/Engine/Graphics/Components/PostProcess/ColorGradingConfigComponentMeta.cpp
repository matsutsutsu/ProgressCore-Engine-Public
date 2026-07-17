#include "Engine/Graphics/Components/PostProcess/ColorGradingConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<ColorGradingConfigComponent> {
        static void Save(json& j, const ColorGradingConfigComponent& c, SaveContext* /*ctx*/) {
            j["enable"]     = c.enable;
            j["tintR"]      = c.data.gradeTint.x;
            j["tintG"]      = c.data.gradeTint.y;
            j["tintB"]      = c.data.gradeTint.z;
            j["saturation"] = c.data.gradeSaturation;
            j["contrast"]   = c.data.gradeContrast;
            j["brightness"] = c.data.gradeBrightness;
            j["mixAmount"]  = c.data.gradeMixAmount;
        }
        static void Load(const json& j, ColorGradingConfigComponent& c, LoadContext* /*ctx*/) {
            if (j.contains("enable"))     c.enable               = j["enable"].get<bool>();
            if (j.contains("tintR"))      c.data.gradeTint.x     = j["tintR"].get<float>();
            if (j.contains("tintG"))      c.data.gradeTint.y     = j["tintG"].get<float>();
            if (j.contains("tintB"))      c.data.gradeTint.z     = j["tintB"].get<float>();
            if (j.contains("saturation")) c.data.gradeSaturation = j["saturation"].get<float>();
            if (j.contains("contrast"))   c.data.gradeContrast   = j["contrast"].get<float>();
            if (j.contains("brightness")) c.data.gradeBrightness = j["brightness"].get<float>();
            if (j.contains("mixAmount"))  c.data.gradeMixAmount  = j["mixAmount"].get<float>();
        }
    };
}

template <> struct ComponentMeta<ColorGradingConfigComponent> {
    static constexpr const char* displayName       = "ColorGrading";
    static constexpr const char* category          = CCL::Categories::Effect;
    static constexpr int         sortPriority       = 545;
    static constexpr bool        hiddenInInspector  = false;
    static constexpr bool        hasCustomGui       = true;
    static constexpr bool        isSerializable     = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ColorGradingConfigComponent& c,
                          unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;
        if (ImGui::Checkbox("有効", &c.enable)) changed = true;

        ImGui::BeginDisabled(!c.enable);
        if (ImGui::ColorEdit3("色補正", &c.data.gradeTint.x)) changed = true;
        if (ImGui::DragFloat("彩度", &c.data.gradeSaturation, 0.01f, 0.0f, 2.0f)) changed = true;
        if (ImGui::DragFloat("コントラスト",   &c.data.gradeContrast,   0.01f, 0.0f, 2.0f)) changed = true;
        if (ImGui::DragFloat("明るさ", &c.data.gradeBrightness, 0.01f, 0.0f, 2.0f)) changed = true;
        if (ImGui::DragFloat("ミックス",        &c.data.gradeMixAmount,  0.01f, 0.0f, 1.0f)) changed = true;

        ImGui::Separator();
        ImGui::TextDisabled("フィルタプリセット");

        if (ImGui::Button("標準")) {
            c.data.gradeTint = { 1.0f, 1.0f, 1.0f, 1.0f };
            c.data.gradeSaturation = 1.0f; c.data.gradeContrast = 1.0f; c.data.gradeBrightness = 1.0f;
            c.data.gradeMixAmount = 1.0f; changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("モノクロ")) {
            c.data.gradeTint = { 1.0f, 1.0f, 1.0f, 1.0f };
            c.data.gradeSaturation = 0.0f; c.data.gradeContrast = 1.1f; c.data.gradeBrightness = 1.0f;
            c.data.gradeMixAmount = 1.0f; changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("セピア")) {
            c.data.gradeTint = { 1.2f, 1.0f, 0.8f, 1.0f };
            c.data.gradeSaturation = 0.25f; c.data.gradeContrast = 1.05f; c.data.gradeBrightness = 1.0f;
            c.data.gradeMixAmount = 1.0f; changed = true;
        }

        if (ImGui::Button("Cool (寒色)")) {
            c.data.gradeTint = { 0.8f, 0.95f, 1.2f, 1.0f };
            c.data.gradeSaturation = 0.9f; c.data.gradeContrast = 1.05f; c.data.gradeBrightness = 1.0f;
            c.data.gradeMixAmount = 1.0f; changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Warm (暖色)")) {
            c.data.gradeTint = { 1.2f, 1.0f, 0.85f, 1.0f };
            c.data.gradeSaturation = 1.05f; c.data.gradeContrast = 1.05f; c.data.gradeBrightness = 1.0f;
            c.data.gradeMixAmount = 1.0f; changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Bleach (退色)")) {
            c.data.gradeTint = { 1.0f, 1.0f, 1.0f, 1.0f };
            c.data.gradeSaturation = 0.4f; c.data.gradeContrast = 1.3f; c.data.gradeBrightness = 1.1f;
            c.data.gradeMixAmount = 1.0f; changed = true;
        }
        ImGui::EndDisabled();

        return changed;
    }
#else
    static bool CustomGui(ColorGradingConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(ColorGradingConfigComponent, "ColorGrading")
REGISTER_COMPONENT_EDITOR_GUI(ColorGradingConfigComponent)
