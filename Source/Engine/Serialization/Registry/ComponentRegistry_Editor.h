#pragma once
// ===========================================================================
// 概要: コンポーネントのインスペクタ描画を登録するマクロ REGISTER_COMPONENT_EDITOR_GUI。
//
// 責務:
//   - エディタ構成（ENABLE_EDITOR）では ComponentGuiRegistry へ描画関数を登録する
//   - それ以外の構成では何も生成しない空のマクロにする
//
// 備考: ComponentRegistry.h から分けてあるのは、エディタでない構成に ImGui を持ち込まないため。
// @code
//   REGISTER_COMPONENT(MyComp, "MyComp")   // 必須
//   REGISTER_COMPONENT_EDITOR_GUI(MyComp)  // エディタ構成でだけ中身がある
// @endcode
// ===========================================================================

#include "Engine/Serialization/Registry/ComponentRegistry.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"

#define REGISTER_COMPONENT_EDITOR_GUI(T)                                                  \
    namespace                                                                             \
    {                                                                                     \
        struct AutoRegGui_##T                                                             \
        {                                                                                 \
            AutoRegGui_##T()                                                              \
            {                                                                             \
                ComponentMetaImGui::RegisterGuiMeta<T>(ComponentGuiRegistry::Instance()); \
            }                                                                             \
        };                                                                                \
    }                                                                                     \
    extern "C" _CCL_USED int g_GuiAnchor_##T = (AutoRegGui_##T{}, 1);                     \
    _CCL_LINK(g_GuiAnchor_##T)
#else
// エディタでない構成では何も生成しない
#define REGISTER_COMPONENT_EDITOR_GUI(T)
#endif