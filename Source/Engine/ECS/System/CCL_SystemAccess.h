#pragma once
// ===========================================================================
// 概要: IfSystem のテンプレート引数に書くアクセス指定子 Read<T> / Write<T>。
//
// 責務:
//   - ForEach のラムダが受け取る引数型（const T& / T&）を決める
//   - 読み書きの別を TaskScheduler の依存解析へ伝える（IsWrite）
// ===========================================================================

namespace CCL::ECS
{
    // 読み取り専用アクセス。ForEach では const T& で渡される
    template <typename T>
    struct Read
    {
        using RawType                 = T;         // 実際のコンポーネント型
        using ParamType               = const T &; // ループ内で受け取る型（const参照）
        static constexpr bool IsWrite = false;
    };

    // 書き込み可能アクセス。ForEach では T& で渡される
    template <typename T>
    struct Write
    {
        using RawType                 = T;   // 実際のコンポーネント型
        using ParamType               = T &; // ループ内で受け取る型（非const参照）
        static constexpr bool IsWrite = true;
    };
} // namespace CCL::ECS
