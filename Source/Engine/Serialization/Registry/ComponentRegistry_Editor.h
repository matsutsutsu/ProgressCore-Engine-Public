// =====================================================================
// ComponentRegistry_Editor.h
//   ImGui (エディタ) 用の登録ヘルパーを切り離したヘッダ。
//   ★ シッピング(エディタなし)ビルドではこのヘッダを include しないこと。
//
//   なぜ分けたか:
//     旧 ComponentRegistry.h は ImGui を強制的に引いていたため、
//     ランタイムビルドからエディタコードを物理的に除外できなかった。
//     これは出荷時のバイナリサイズ・起動時間・セキュリティ全てに悪影響。
//
//   使い方:
//     コンポーネントの .cpp 内で
//       REGISTER_COMPONENT(MyComp, "MyComp");           // 必須 (ランタイム)
//       REGISTER_COMPONENT_EDITOR_GUI(MyComp);          // エディタビルドのみ
//
//   ビルド構成:
//     - ENABLE_EDITOR が定義されているプロジェクト  : 両方有効
//     - 定義されていないプロジェクト              : EDITOR_GUI 側は空マクロ
// =====================================================================
#pragma once

#include "Engine/Serialization/Registry/ComponentRegistry.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"

#define REGISTER_COMPONENT_EDITOR_GUI(T)                                                          \
        namespace {                                                                                   \
            struct AutoRegGui_##T {                                                                   \
                AutoRegGui_##T() {                                                                    \
                    ComponentMetaImGui::RegisterGuiMeta<T>(ComponentGuiRegistry::Instance());         \
                }                                                                                     \
            };                                                                                        \
        }                                                                                             \
        extern "C" _CCL_USED int g_GuiAnchor_##T = (AutoRegGui_##T{}, 1);                             \
        _CCL_LINK(g_GuiAnchor_##T)
#else
    // ランタイム専用ビルド: 完全に空のマクロ。一切コードを生成しない。
#define REGISTER_COMPONENT_EDITOR_GUI(T)
#endif