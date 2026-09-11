// VignetteConfigComponent のシリアライズ/インスペクタ定義(パターンC)。
// 画面端の減光/着色パラメータを編集可能にする。
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<VignetteConfigComponent> {
        static void Save(json& j, const VignetteConfigComponent& c, SaveContext* /*ctx*/) {
            j["enable"]     = c.enable;
            j["colorR"]     = c.data.vigColor.x;
            j["colorG"]     = c.data.vigColor.y;
            j["colorB"]     = c.data.vigColor.z;
            j["intensity"]  = c.data.vigIntensity;
            j["radius"]     = c.data.vigRadius;
            j["softness"]   = c.data.vigSoftness;
            j["pulseSpeed"] = c.data.vigPulseSpeed;
        }
        static void Load(const json& j, VignetteConfigComponent& c, LoadContext* /*ctx*/) {
            if (j.contains("enable"))     c.enable            = j["enable"].get<bool>();
            if (j.contains("colorR"))     c.data.vigColor.x   = j["colorR"].get<float>();
            if (j.contains("colorG"))     c.data.vigColor.y   = j["colorG"].get<float>();
            if (j.contains("colorB"))     c.data.vigColor.z   = j["colorB"].get<float>();
            if (j.contains("intensity"))  c.data.vigIntensity = j["intensity"].get<float>();
            if (j.contains("radius"))     c.data.vigRadius    = j["radius"].get<float>();
            if (j.contains("softness"))   c.data.vigSoftness  = j["softness"].get<float>();
            if (j.contains("pulseSpeed")) c.data.vigPulseSpeed = j["pulseSpeed"].get<float>();
        }
    };
}

template <> struct ComponentMeta<VignetteConfigComponent> {
    static constexpr const char* displayName       = "Vignette";
    static constexpr const char* category          = CCL::Categories::Effect;
    static constexpr int         sortPriority       = 540;
    static constexpr bool        hiddenInInspector  = false;
    static constexpr bool        hasCustomGui       = true;
    static constexpr bool        isSerializable     = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(VignetteConfigComponent& c,
                          unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        namespace mi = ComponentMetaImGui;
        bool changed = false;

        // 1行ヘルパーで「ラベル左・幅いっぱい」に統一（自動グリッドと同じ見た目）。
        changed |= mi::Checkbox("有効", &c.enable);

        ImGui::BeginDisabled(!c.enable);
        changed |= mi::Color3   ("色",       &c.data.vigColor.x);
        changed |= mi::DragFloat("強度",     &c.data.vigIntensity, 0.01f, 0.0f, 1.0f);
        changed |= mi::DragFloat("半径",     &c.data.vigRadius,    0.01f, 0.0f, 1.0f);
        changed |= mi::DragFloat("柔らかさ", &c.data.vigSoftness,  0.01f, 0.01f, 1.0f);
        changed |= mi::DragFloat("脈動速度", &c.data.vigPulseSpeed, 0.05f, 0.0f, 8.0f);

        ImGui::Separator();
        if (ImGui::Button("プリセット:弱め(常時)")) {
            c.data.vigColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            c.data.vigIntensity = 0.4f;
            c.data.vigRadius = 0.55f;
            c.data.vigSoftness = 0.35f;
            c.data.vigPulseSpeed = 0.0f;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("プリセット:危険(HP低下)")) {
            c.data.vigColor = { 0.5f, 0.0f, 0.0f, 1.0f };
            c.data.vigIntensity = 0.7f;
            c.data.vigRadius = 0.35f;
            c.data.vigSoftness = 0.4f;
            c.data.vigPulseSpeed = 2.0f;
            changed = true;
        }
        ImGui::EndDisabled();

        return changed;
    }
#else
    static bool CustomGui(VignetteConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(VignetteConfigComponent, "Vignette")
REGISTER_COMPONENT_EDITOR_GUI(VignetteConfigComponent)
