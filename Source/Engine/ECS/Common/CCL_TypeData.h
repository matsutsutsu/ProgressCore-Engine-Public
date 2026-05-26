/**
 * @file   CCL_TypeData.h
 * @brief  コンポーネント型ごとのメタ情報(TypeInfo)とランタイムメタ情報(TypeData)
 *
 * @details
 *  ECSでは「コンポーネント型T」を生メモリで扱うため、型を忘れた後でも
 *  「サイズ」「破棄方法」「コピー方法」「移動方法」が分かる必要があります。
 *  本ファイルではそれらを以下の2つに分けて提供します:
 *
 *   - TypeInfo<T> : コンパイル時に決まる型情報(ID, size, 型名)
 *                   inline static変数による「型ごと一意のID」発行
 *   - TypeData    : 実行時に保持される型情報(関数ポインタの集合体)
 *                   Archetype や Chunk に値で格納される
 *
 *  【依存】
 *   - CCL_IDs.h       (TypeID等の基本型)
 *   - CCL_Destructor.h (Destructor)
 *   - \<type_traits\> (コピー可能性などの判定)
 *   - \<new\>         (placement new)
 *
 *  【ビルド速度への配慮】
 *  TypeData::Create\<T\>() はラムダ式を多用するため重いコードですが、
 *  これは「コンポーネント型を新規登録するファイル」だけが触れます。
 *  単に EntityID や TypeID を使うだけのファイルは CCL_IDs.h で十分です。
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_IDs.h"
#include "CCL_Destructor.h"

#include <type_traits>
#include <new>
#include <cassert>


namespace CCL::ECS
{
    // =========================================================================
    //  TypeInfo<T> : 型ごとに一意なIDを発行
    // =========================================================================

    /**
     * @class TypeInfo
     * @brief 型Tに対応するコンパイル時メタ情報を提供する
     *
     * @tparam T 任意のコンポーネント型(POD/非POD問わない)
     *
     * @details
     *  C++17の inline static変数を活用しており、複数翻訳単位を跨いでも
     *  TypeInfo<MyComponent>::ID() は常に同じ値を返します。
     *  内部で ID_Emitter::EmitTypeID() を1回だけ呼び出すため、
     *  型ごとに一意のTypeIDが付与されます。
     *
     *  【使用例】
     *  @code
     *      TypeID id = TypeInfo<Position>::ID();
     *      size_t sz = TypeInfo<Position>::Size();
     *  @endcode
     */
    template <class T>
    class TypeInfo
    {
    public:
        /**
         * @brief 型Tに割り当てられたTypeID
         * @details inline static により、全翻訳単位で同一の値を共有します。
         */
        inline static const TypeID s_id = ID_Emitter::EmitTypeID();

        /// @brief 型TのTypeIDを返す
        static TypeID ID() noexcept { return s_id; }

        /// @brief 型Tのsizeofを返す
        static constexpr std::size_t Size() noexcept { return sizeof(T); }

        /**
         * @brief 型名を文字列で返す(デバッグ用)
         * @details typeid を使用するため、コンパイラ依存の文字列(例: "struct Position")
         *          が返ります。Logger等で型名を表示する用途を想定しています。
         */
        static const char* Name();
    };


    // =========================================================================
    //  TypeData : 実行時メタ情報の集合体
    // =========================================================================

    /**
     * @class TypeData
     * @brief コンポーネント型Tの「実行時操作セット」をひとまとめにした構造体
     *
     * @details
     *  以下を関数ポインタ/ラムダの形で保持し、生メモリに対して型安全な操作を行います:
     *   - destructor          : ~T() を呼ぶ
     *   - constructor         : 別メモリからのコピー構築 (Placement New + copy ctor)
     *   - assigner            : 既存オブジェクトへのコピー代入 (operator=)
     *   - mover               : 別メモリからのムーブ構築 (Placement New + std::move)
     *   - defaultConstructor  : デフォルト構築 (T() 相当)
     *
     *  Create\<T\>() を1回呼ぶだけで全ての関数ポインタを設定します。
     *
     *  【SFINAE的考慮】
     *  std::is_copy_constructible_v<T> 等で型特性を確認し、
     *  コピー不可能な型(unique_ptrを持つ型など)に対しては安全な
     *  「アサート版」関数を割り当てます。
     */
    class TypeData
    {
    public:
        // =====================================================================
        //  関数ポインタの型エイリアス
        // =====================================================================

        /// @brief コピー構築用 (dst未初期化, srcから構築)
        using ConstructFunc        = void(*)(void*, const void*);
        /// @brief コピー代入用 (dst既構築済み, srcから代入)
        using AssignFunc           = void(*)(void*, const void*);
        /// @brief ムーブ構築用 (dst未初期化, srcから移動構築)
        using MoveFunc             = void(*)(void*, void*);
        /// @brief デフォルト構築用 (dst未初期化, T()で構築)
        using DefaultConstructFunc = void(*)(void*);


        // =====================================================================
        //  デフォルト関数(全部 default で生成)
        // =====================================================================

        TypeData()                                = default;
        ~TypeData()                               = default;
        TypeData(const TypeData&)                 = default;
        TypeData& operator=(const TypeData&)      = default;
        TypeData(TypeData&&) noexcept             = default;
        TypeData& operator=(TypeData&&) noexcept  = default;


        // =====================================================================
        //  初期化
        // =====================================================================

        /**
         * @brief 型Tに関する全メタ情報を初期化する
         * @tparam T 任意のコンポーネント型
         *
         * @details
         *  型特性に応じて以下のラムダを生成し、関数ポインタとして保存します。
         *  - コピー構築可能 : 通常のコピー
         *  - コピー構築不可能: アサート発火するスタブ(間違って呼ばれた時の保険)
         */
        template <class T>
        void Create()
        {
            id        = TypeInfo<T>::ID();
            typeSize  = TypeInfo<T>::Size();
            name      = TypeInfo<T>::Name();

            destructor.template Create<T>();

            // ----- デフォルト構築 ---------------------------------------------
            if constexpr (std::is_default_constructible_v<T>) {
                defaultConstructor = [](void* dst) { new (dst) T(); };
            } else {
                defaultConstructor = [](void*) {
                    assert(false && "Default constructor called for non-default-constructible type");
                };
            }

            // ----- コピー構築 -------------------------------------------------
            if constexpr (std::is_copy_constructible_v<T>) {
                constructor = [](void* dst, const void* src) {
                    new (dst) T(*reinterpret_cast<const T*>(src));
                };
            } else {
                constructor = [](void*, const void*) {
                    assert(false && "Copy constructor called for non-copyable type");
                };
            }

            // ----- コピー代入 -------------------------------------------------
            if constexpr (std::is_copy_assignable_v<T>) {
                assigner = [](void* dst, const void* src) {
                    *reinterpret_cast<T*>(dst) = *reinterpret_cast<const T*>(src);
                };
            } else {
                assigner = [](void*, const void*) {
                    assert(false && "Copy assignment called for non-copy-assignable type");
                };
            }

            // ----- ムーブ構築 -------------------------------------------------
            // 必ずplacement new + std::moveを使う(代入ではなく構築)
            // dstは未初期化メモリのため operator= は不可
            if constexpr (std::is_move_constructible_v<T>) {
                mover = [](void* dst, void* src) {
                    new (dst) T(std::move(*reinterpret_cast<T*>(src)));
                };
            } else {
                // ムーブ不可ならコピーで代替(コピーも不可ならアサート)
                if constexpr (std::is_copy_constructible_v<T>) {
                    mover = [](void* dst, void* src) {
                        new (dst) T(*reinterpret_cast<const T*>(src));
                    };
                } else {
                    mover = [](void*, void*) {
                        assert(false && "Move called for non-movable, non-copyable type");
                    };
                }
            }
        }


        // =====================================================================
        //  比較演算子 (Archetype内のソート/一致判定で使用)
        // =====================================================================

        /// @brief TypeIDによる順序比較(Archetypeソート用)
        bool operator<(const TypeData& r)  const noexcept { return id <  r.id; }

        /// @brief TypeIDによる等値判定
        bool operator==(const TypeData& r) const noexcept { return id == r.id; }


        // =====================================================================
        //  メンバ変数(public でOK ─ POD的に扱う)
        // =====================================================================

        /// @brief 型のID (TypeInfo<T>::ID() と一致)
        TypeID                 id        = InvalidTypeID;
        /// @brief 型のsizeof
        std::size_t            typeSize  = 0;
        /// @brief 型名(デバッグ用, 静的文字列を指す)
        const char*            name      = nullptr;

        Destructor             destructor;          ///< 破棄関数
        ConstructFunc          constructor        = nullptr; ///< コピー構築関数
        AssignFunc             assigner           = nullptr; ///< コピー代入関数
        MoveFunc               mover              = nullptr; ///< ムーブ構築関数
        DefaultConstructFunc   defaultConstructor = nullptr; ///< デフォルト構築関数
    };


    // =========================================================================
    //  TypeInfo<T>::Name() の実装
    // =========================================================================
    // 注: typeidヘッダの include を Name() の呼び出し側のみに局所化するため、
    //     ヘッダ末尾で .cpp ライクに定義しています。

} // namespace CCL::ECS

// ─── 型名取得用に <typeinfo> を遅延include ──────────────────────────────────
// (Name()を実際に呼ぶ翻訳単位だけがコストを払う)
#include <typeinfo>

namespace CCL::ECS
{
    template <class T>
    const char* TypeInfo<T>::Name()
    {
        // 関数ローカル静的変数で1度だけ評価
        static const char* s_name = typeid(T).name();
        return s_name;
    }
}
