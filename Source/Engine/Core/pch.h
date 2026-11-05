#pragma once
// ===========================================================================
// 概要: プリコンパイル済みヘッダ。/FI で全 .cpp の先頭へ自動で挿入される
//
// 備考: 含めてよいのは、標準ライブラリ・Windows / DirectX・安定したサードパーティだけ
//       自作ヘッダ・一部でしか使わないヘッダ・#define で挙動が変わるもの（Tracy など）は入れない
//       追加前のチェックリストとビルドエラーの対処は、このファイルの末尾を参照
// ===========================================================================

// ============================================================
// 1. プラットフォーム・マクロ設定
// ============================================================
// Windows.h を include する前に、不要な機能を無効化するマクロを定義する
// 順序を間違えるとマクロが効かなくなるため、必ずファイル冒頭に置く

// 不要な Windows API（ネットワーク・コマンドライン引数解析等）を除外する
// ビルドが速くなり、シンボルの衝突も減る
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Windows.h が定義する min/max マクロを無効化する
// これがないと std::min / std::max と衝突し、至る所でビルドエラーになる
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
// 1つ1つが巨大なため、PCH に入れる効果が最も大きい区画

#include <Windows.h>
#include <windowsx.h>

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
// ECS の JobSystem / ChunkManager で多用されるため、PCH に入れる効果が大きい

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
// プロジェクト設定が C++20 以上のときだけ取り込む

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
// ヘッダを足す前に確かめる: 過半数の .cpp で使うか、変更がまれか、#define で挙動が変わらないか（Tracy など）
// 自作のヘッダ（ECS など）は足さない。変えるたびに全部の .cpp が再ビルドになる
// NOMINMAX が効かないときは、ほかのヘッダが pch.h より先に Windows.h を include していないかを見る
// 出し入れの効果は、前後でソリューションのリビルドの時間を比べて確かめる
