#pragma once
// コンポーネントのインスペクタ描画を登録するマクロ。エディタでない構成では空になる
// ComponentRegistry.h から分けて、エディタでない構成に ImGui を持ち込まない

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