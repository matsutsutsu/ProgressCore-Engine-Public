#pragma once
// ===========================================================================
// 概要: プリコンパイル済みヘッダ（PCH）。
//
// 責務:
//   - 多くの .cpp で使う「重い」「めったに変わらない」ヘッダを事前コンパイルし、
//     ビルド時間を短くする
//
// 備考:
//   【適用方式】
//     強制インクルード方式（/FI pch.h）。各 .cpp に #include "pch.h" を書く必要はなく、
//     プロジェクト設定で自動的に先頭へ挿入される。
//
//   【含めてよいもの】
//     - 標準ライブラリ（変更されない）
//     - Windows / DirectX（巨大で頻繁に include される）
//     - 安定したサードパーティライブラリ
//
//   【含めてはいけないもの】
//     - 自作ヘッダ（変更されると全体の再ビルドになる）
//     - 特定の .cpp でしか使わないヘッダ（オーバーヘッドだけが残る）
//     - #define で挙動が切り替わるもの（Tracy 等）
//
//   追加前のチェックリストとビルドエラー対処は、このファイルの末尾を参照。
// ===========================================================================

// ============================================================
// 1. プラットフォーム・マクロ設定
// ============================================================
// Windows.h を include する前に、不要な機能を無効化するマクロを定義する。
// 順序を間違えるとマクロが効かなくなるため、必ずファイル冒頭に置く。

// 不要な Windows API（ネットワーク・コマンドライン引数解析等）を除外する。
// ビルドが速くなり、シンボルの衝突も減る
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Windows.h が定義する min/max マクロを無効化する。
// これが無いと std::min / std::max と衝突し、至る所でビルドエラーになる
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Windows ヘッダをさらに軽くする（一部の不要なヘッダを除外）
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

// ============================================================
// 2. Windows / DirectX 11
// ============================================================
// 1つ1つが巨大なため、PCH に入れる効果が最も大きい区画。

#include <Windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM 等のマクロ

// --- DirectX 11 系 ---
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXColors.h>

// --- Microsoft::WRL::ComPtr（COM オブジェクトのスマートポインタ） ---
#include <wrl/client.h>

// COM オブジェクトを解放して nullptr にする。例: SAFE_RELEASE(m_pDevice);
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)     \
    do                      \
    {                       \
        if (p)              \
        {                   \
            (p)->Release(); \
            (p) = nullptr;  \
        }                   \
    } while (0)
#endif

// ============================================================
// 3. 標準ライブラリ - 基本型・ユーティリティ
// ============================================================

// --- 基本型 / 数値 ---
#include <cstdint>
#include <cstddef>
#include <climits>
#include <limits>
#include <cmath>
#include <cstdlib>

// --- 文字列・文字 ---
#include <cstring>
#include <cctype>
#include <cwchar>
#include <string>
#include <string_view>

// --- 例外・診断 ---
#include <cassert>
#include <stdexcept>
#include <system_error>

// ============================================================
// 4. 標準ライブラリ - コンテナ
// ============================================================

#include <vector>
#include <array>
#include <deque>
#include <list>
#include <forward_list>

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <queue>
#include <stack>
#include <bitset>

// ============================================================
// 5. 標準ライブラリ - メモリ・関数・タプル
// ============================================================

#include <memory>
#include <functional>
#include <utility>
#include <tuple>
#include <optional>
#include <variant>
#include <any>

// ============================================================
// 6. 標準ライブラリ - アルゴリズム・型情報・イテレータ
// ============================================================

#include <algorithm>
#include <numeric>
#include <iterator>
#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include <initializer_list>

// ============================================================
// 7. 標準ライブラリ - 並行・時間
// ============================================================
// ECS の JobSystem / ChunkManager で多用されるため、PCH に入れる効果が大きい。

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <condition_variable>
#include <future>
#include <chrono>

// ============================================================
// 8. 標準ライブラリ - I/O・ストリーム
// ============================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

// ============================================================
// 9. C++20 機能
// ============================================================
// プロジェクト設定が C++20 以上のときだけ取り込む。

#if __cplusplus >= 202002L || _MSVC_LANG >= 202002L
#include <concepts>
#include <ranges>
#include <span>
#include <bit>
#endif

// ============================================================
// メンテナンスメモ
// ============================================================
//
// 1) 【ヘッダ追加時のチェックリスト】
//    □ 本当に過半数の .cpp で使われるか？
//    □ 変更頻度は十分に低いか？（少なくとも月単位で変わらない）
//    □ #define で挙動が変わるヘッダではないか？（Tracy / TRACY_ENABLE 等）
//
// 2) 【ECS / 自作ヘッダの追加は禁止】
//    自作ヘッダを追加すると、そのヘッダを変えるたびに全 .cpp が再ビルドされ、
//    ヘッダを細かく分割してビルドを軽くした意味が無くなる。
//
// 3) 【ビルドエラー対処】
//    NOMINMAX が効かない場合：
//      → 他のヘッダ（例: gdiplus.h）が pch.h より先に Windows.h を
//        include していないかチェック。pch.h は必ず最初に include される
//        ように /FI で強制 include していること。
//
//    min/max のシンボル衝突が起きる場合：
//      → コード側で `(std::min)(a, b)` のように丸括弧で囲むと
//        マクロ展開を回避できる。
//
// 4) 【効果の計測】
//    ヘッダの出し入れの前後で「ソリューションのリビルド」の時間を比べる
//    （出力ウィンドウ末尾に経過時間が出る）。効果が薄ければ選定を見直す。
