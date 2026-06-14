/// @file  BulletComponentsMeta.cpp
/// @brief BulletProgramRef / BulletStateComponent / BulletTag の
///        シリアライズ/インスペクタ定義。
///        いずれも実行時専用（弾は毎フレーム生滅する）のため isSerializable = false。
///        インスペクタではデバッグ目的の読み取り表示のみ行う。

#include "Game/Logics/Wand/Bullet/BulletComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// BulletProgramRef
//   ProgramID（uint32_t）を1つ持つ軽量参照。
//   シリアライズ不要。インスペクタで「どのプログラムを参照しているか」を確認用。
// ===========================================================================
template <> struct ComponentMeta<BulletProgramRef>
{
    CCL_META_USING_THIS(BulletProgramRef)
        static constexpr const char* displayName = "BulletProgramRef";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // ランタイム専用

#ifdef ENABLE_EDITOR
    static bool CustomGui(BulletProgramRef& comp,
        unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr)
    {
        // ProgramID は書き換えさせない（Wand側が管理する値）。読み取り表示のみ。
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
        return empty; // CustomGui で表示するため Fields は空
    }
};

REGISTER_COMPONENT(BulletProgramRef, "BulletProgramRef")
REGISTER_COMPONENT_EDITOR_GUI(BulletProgramRef)


// ===========================================================================
// BulletStateComponent
//   弾の飛行状態（向き・速度・経過時間・寿命・フラグ）。
//   毎フレーム書き換わるランタイムデータ。シリアライズ不要。
//   インスペクタでは全フィールドを読み取り専用で表示し、デバッグを助ける。
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
        // 読み取り専用表示（弾の状態はSystemが書くため手動編集させない）
        ImGui::SeparatorText("Motion");
        ImGui::LabelText("Direction", "(%.2f, %.2f, %.2f)",
            comp.direction.x, comp.direction.y, comp.direction.z);
        ImGui::LabelText("Speed", "%.2f", comp.speed);

        ImGui::SeparatorText("Lifetime");
        // プログレスバーで寿命の残量を可視化
        const float ratio = (comp.lifetime > 0.0f) ? (comp.age / comp.lifetime) : 1.0f;
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f / %.2f sec", comp.age, comp.lifetime);
        ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), overlay);

        ImGui::SeparatorText("Debug");
        ImGui::LabelText("Inherit Depth", "%u", comp.inheritanceDepth);

        // hitフラグを色付きで表示（trueが赤で目立つ）
        if (comp.hitWall)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "HIT WALL");
        }
        else
        {
            ImGui::TextDisabled("Hit Wall: -");
        }

        if (comp.hitEnemy)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "HIT ENEMY");
        }
        else
        {
            ImGui::TextDisabled("Hit Enemy: -");
        }

        return false; // 読み取り専用なので常に false
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
// BulletTag
//   「これは魔法弾である」ことを示す空タグ。
//   System フィルタ専用。インスペクタにはラベルだけ表示。
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