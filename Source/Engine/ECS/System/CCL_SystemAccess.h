/**
 * @file   CCL_SystemAccess.h
 * @brief  Systemが各コンポーネント型を「読み取り専用」「書き込み可」のどちらで使うか宣言するためのタグ型
 *
 * @details
 *  ForEachループ内で受け取る型を、テンプレートメタプログラミングで自動的に
 *  `const T&` または `T&` に切り替えるための仕組みです。
 *  これによりSystem定義時に「うっかり書き込んでしまう」ミスをコンパイル時に防げます。
 *
 *  @par 使用例
 *  @code
 *      class MovementSystem : public IfSystem<MovementSystem,
 *                                             Write<Position>,
 *                                             Read<Velocity>>
 *      {
 *          void Update(float dt) override {
 *              ForEach([dt](Position& pos, const Velocity& vel) {
 *                  // pos は書き換え可、vel は const → うっかり書き込み防止
 *              });
 *          }
 *      };
 *  @endcode
 *
 *  TaskSchedulerは Read/Write 情報からデータ競合を検出し、並列バッチを構築します。
 *
 * @author CCL ECS Library
 */
#pragma once

namespace CCL::ECS
{
    /**
     * @struct Read
     * @brief 読み取り専用アクセスを宣言するタグ型
     * @tparam T 対象コンポーネント型
     */
    template <typename T>
    struct Read
    {
        using RawType   = T;            ///< 実コンポーネント型
        using ParamType = const T&;     ///< ForEachで受け取る型
        static constexpr bool IsWrite = false;
    };

    /**
     * @struct Write
     * @brief 書き込み可能アクセスを宣言するタグ型
     * @tparam T 対象コンポーネント型
     */
    template <typename T>
    struct Write
    {
        using RawType   = T;            ///< 実コンポーネント型
        using ParamType = T&;           ///< ForEachで受け取る型
        static constexpr bool IsWrite = true;
    };

} // namespace CCL::ECS
