/// @file  RootMotionComponentMeta.cpp
/// @brief RootMotionComponent のメタ定義。
///
/// 設計方針(Why):
///   このコンポーネントが保持する値 (enabled, prevProgress, justStarted) は
///   全てシステムが毎フレーム書き換える純粋な「実行時(Runtime)データ」である。
///   そのため、中身の数値はシリアライズ（保存）による汚染を防ぐ。
///   ただし、コンポーネント自体がアタッチされている「状態」はプレハブ等に
///   残す必要があるため、パターンC（カスタムシリアライズ）を用いて
///   空枠のみをセーブ・ロードする設計としている。

#include "RootMotionComponent.h"

// ★ マニュアル絶対ルール：必須ヘッダー
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h" // パターンCに必須

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// パターンC: カスタムシリアライズ (空枠のみ保存)
// ===========================================================================
namespace ComponentMetaJson
{
template <>
struct ComponentSerializer<RootMotionComponent>
{
    static void Save(json & /*j*/, const RootMotionComponent & /*c*/, SaveContext * /*ctx*/)
    {
        // 中身の変数は実行時データなので保存しない（親キーと空枠 "{}" だけが書き出される）
    }
    static void Load(const json & /*j*/, RootMotionComponent & /*c*/, LoadContext * /*ctx*/)
    {
        // ロード時も変数を上書きせず、構造体の初期値 (false, 0.0f, false) をそのまま維持する
    }
};
} // namespace ComponentMetaJson

// ===========================================================================
// ComponentMeta<T> 本体
// ===========================================================================
template <>
struct ComponentMeta<RootMotionComponent>
{
    CCL_META_USING_THIS(RootMotionComponent)

    static constexpr const char *displayName    = "Root Motion";
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true; // ★ true: コンポーネントのアタッチ状態は保存

#ifdef ENABLE_EDITOR
    static bool CustomGui(RootMotionComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        ImGui::TextDisabled("[Runtime State: システムによって自動制御されます]");
        ImGui::Separator();

        // 実行時データのため、ユーザーの手動操作を物理的にロックする（ReadOnly化）
        ImGui::BeginDisabled();

        ImGui::Checkbox("Enabled", &comp.enabled);
        ImGui::DragFloat("Prev Timer", &comp.prevTimer, 0.01f, 0.0f, 10.0f);

        ImGui::EndDisabled();

        // 読み取り専用のため、Undo/Redo用の変更フラグは常に false でよい
        return false;
    }
#else
    // ★ マニュアル絶対ルール：フォールバック
    static bool CustomGui(RootMotionComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    // パターンC なので Fields は空を返す
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(RootMotionComponent, "Animation")
REGISTER_COMPONENT_EDITOR_GUI(RootMotionComponent)