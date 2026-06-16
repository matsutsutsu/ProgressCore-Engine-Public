// ===========================================================================
// BulletComponentsMeta.cpp
// ------------------------
// 役割:
//   BulletProgramRef / BulletStateComponent / BulletTag のシリアライズ・
//   インスペクタ定義。いずれも実行時専用(弾は毎フレーム生滅)のため
//   isSerializable = false。インスペクタはデバッグ目的の読み取り表示のみ。
// ===========================================================================

#include "Game/Logics/Wand/Bullet/BulletComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// BulletProgramRef : ProgramID を1つ持つ軽量参照。読み取り表示のみ。
// ===========================================================================
template <> struct ComponentMeta<BulletProgramRef>
{
    CCL_META_USING_THIS(BulletProgramRef)
        static constexpr const char* displayName = "BulletProgramRef";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false;

#ifdef ENABLE_EDITOR
    static bool CustomGui(BulletProgramRef& comp,
        unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr)
    {
        ImGui::LabelText("Program ID",
            comp.programId == InvalidProgramID ? "INVALID" : "%u", comp.programId);
        return false;
    }
#else
    static bool CustomGui(BulletProgramRef&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(BulletProgramRef, "BulletProgramRef")
REGISTER_COMPONENT_EDITOR_GUI(BulletProgramRef)


// ===========================================================================
// BulletStateComponent : 弾の飛行状態。読み取り専用表示でデバッグを助ける。
// ===========================================================================
template <> struct ComponentMeta<BulletStateComponent>
{
    CCL_META_USING_THIS(BulletStateComponent)
        static constexpr const char* displayName = "BulletState";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false;

#ifdef ENABLE_EDITOR
    static bool CustomGui(BulletStateComponent& comp,
        unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr)
    {
        ImGui::SeparatorText("Motion");
        ImGui::LabelText("Direction", "(%.2f, %.2f, %.2f)",
            comp.direction.x, comp.direction.y, comp.direction.z);
        ImGui::LabelText("Speed", "%.2f", comp.speed);

        ImGui::SeparatorText("Lifetime");
        const float ratio = (comp.lifetime > 0.0f) ? (comp.age / comp.lifetime) : 1.0f;
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f / %.2f sec", comp.age, comp.lifetime);
        ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), overlay);

        ImGui::SeparatorText("Debug");
        ImGui::LabelText("Inherit Depth", "%u", comp.inheritanceDepth);

        // 見た目付与済みか
        if (comp.visualAttached)
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Visual: attached");
        else
            ImGui::TextDisabled("Visual: -");

        // hitフラグ
        if (comp.hitWall)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "HIT WALL");
        else
            ImGui::TextDisabled("Hit Wall: -");

        if (comp.hitEnemy)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "HIT ENEMY");
        else
            ImGui::TextDisabled("Hit Enemy: -");

        return false;
    }
#else
    static bool CustomGui(BulletStateComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(BulletStateComponent, "BulletState")
REGISTER_COMPONENT_EDITOR_GUI(BulletStateComponent)


// ===========================================================================
// BulletTag : 空タグ。ラベルのみ表示。
// ===========================================================================
template <> struct ComponentMeta<BulletTag>
{
    CCL_META_USING_THIS(BulletTag)
        static constexpr const char* displayName = "BulletTag";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false;

#ifdef ENABLE_EDITOR
    static bool CustomGui(BulletTag& /*comp*/,
        unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr)
    {
        ImGui::TextDisabled("(Magic bullet identifier tag)");
        return false;
    }
#else
    static bool CustomGui(BulletTag&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(BulletTag, "BulletTag")
REGISTER_COMPONENT_EDITOR_GUI(BulletTag)