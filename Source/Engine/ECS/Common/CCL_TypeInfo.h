#pragma once
// ===========================================================================
// 概要: 任意の型 T の「型ID・サイズ・型名・構築/破棄関数」を提供する型情報レイヤー。
//
// 責務:
//   - TypeInfo<T>: 型 T → 数値 ID の変換（コンパイル時テンプレート）
//   - TypeData:    実行時に持ち回る型情報レコード（id / size / 関数ポインタ）
// ===========================================================================

#include <cstddef>
#include <string>
#include <new>         // placement new
#include <type_traits> // std::is_copy_constructible_v 等
#include <typeinfo>
#include <cassert>
#include <utility> // std::move

#include "CCL_Types.h"
#include "CCL_IDEmitter.h"
#include "CCL_TypeErasure.h"

namespace CCL::ECS
{
    // ============================================================
    // TypeInfo<T>
    // ============================================================

    /**
     * @brief 任意のコンポーネント型 T に「ユニークな数値 ID」を割り当てる
     *        テンプレートユーティリティ。
     *
     * 型そのものではなく数値 ID で比較することで、Archetype の一致判定や
     * コンポーネント配置オフセットの計算を速くする。
     *
     * @code
     *   TypeInfo<Position>::ID();   // 例: 1
     *   TypeInfo<Velocity>::ID();   // 例: 2
     *   TypeInfo<Position>::ID();   // 例: 1（必ず同じ値）
     * @endcode
     */
    template <class T>
    class TypeInfo
    {
    public:
        /**
         * @brief 型 T に紐づく永続ユニーク ID を返す。
         *
         * @note 採番を「関数ローカル static」で行うのが要点。初回呼び出し時に
         *       初期化されるので、静的初期化中に呼ばれても必ず正しい値を返す。
         *
         *       `inline static` メンバにしてはいけない。動的初期化の順序が翻訳単位を
         *       またいで保証されない(unordered initialization)ため、静的初期化中に走る
         *       REGISTER_COMPONENT がゼロ初期化のままの 0 を読む。ComponentRegistry の
         *       TypeID→名前の対応表が 0 番に化け、「プレハブは読み込めるのに保存すると
         *       特定コンポーネントが消える」という無言の破損になる
         *       (どの型が壊れるかはリンク順次第)。
         *       ID_Emitter が関数ローカル static なのも同じ理由。
         */
        static TypeID ID()
        {
            static const TypeID s_id = ID_Emitter::EmitTypeID();
            return s_id;
        }

        /**
         * @brief sizeof(T) を返す。
         *
         * Chunk が SoA 配置を計算するのに使う
         * （先頭アドレス + entityIndex * Size() で各 Entity の番地が決まる）。
         */
        static size_t Size()
        {
            return sizeof(T);
        }

        // 型名（実装依存。デバッグ・シリアライズで使用）
        static const char *Name()
        {
            static std::string name = typeid(T).name(); // "struct Transform" 等
            return name.c_str();
        }
    };

    // ============================================================
    // TypeData
    // ============================================================

    /**
     * @brief 任意のコンポーネント型 T を「実行時に持ち回れる情報」として
     *        パッケージ化したレコード。
     *
     * TypeInfo<T> がコンパイル時テンプレートなのに対し、TypeData は
     * 実行時に std::vector に格納したり、関数引数で渡したりするための
     * 「型情報の運び屋」。ECS が各コンポーネント型を「メモリ操作できるデータ」
     * として一律に扱うための中核データ構造。
     *
     * 含まれる情報:
     *   - id                 : コンポーネント型を識別する ID（整数比較で済む）
     *   - typeSize           : 1個あたりのメモリサイズ
     *   - destructor         : デストラクタ呼び出しを型消去で持つ
     *   - constructor        : placement new によるコピー構築
     *   - assigner           : operator= による代入
     *   - mover              : ムーブ構築（placement new + std::move）
     *   - defaultConstructor : デフォルトコンストラクタ呼び出し
     *   - name               : デバッグ用の型名
     */
    class TypeData
    {
    public:
        TypeData()  = default;
        ~TypeData() = default;

        // vector 等に格納するため、コピー・ムーブは全て有効
        TypeData(const TypeData &)            = default;
        TypeData &operator=(const TypeData &) = default;
        TypeData(TypeData &&)                 = default;
        TypeData &operator=(TypeData &&)      = default;

        /**
         * @brief 型 T から TypeData の各フィールドを一括初期化する。
         *
         * id / size / 各種構築関数（コピー・ムーブ・代入・デフォルト構築）と
         * デストラクタを一気に登録する。
         *
         * @note コピーが禁止された型でもコンパイルが通るよう、`if constexpr` で
         *       コピー可能かをコンパイル時に判定している。禁止型のコピーを
         *       実行時に呼んでしまった場合は assert で止まる。
         */
        template <class T>
        void Create()
        {
            id       = TypeInfo<T>::ID();
            typeSize = TypeInfo<T>::Size();
            destructor.template Create<T>();
            name = TypeInfo<T>::Name();

            // デフォルト構築
            defaultConstructor = [](void *dst) {
                new (dst) T();
            };

            // コピー構築（placement new）。
            // 未初期化メモリへの新規構築。assigner と違って operator= は呼ばない。
            if constexpr (std::is_copy_constructible_v<T>)
            {
                constructor = [](void *dst, const void *src) {
                    new (dst) T(*reinterpret_cast<const T *>(src));
                };
            }
            else
            {
                constructor = [](void *, const void *) {
                    assert(false && "Copy constructor called for non-copyable type!");
                };
            }

            // コピー代入（operator=）
            if constexpr (std::is_copy_assignable_v<T>)
            {
                assigner = [](void *dst, const void *src) {
                    T       *dstObj = reinterpret_cast<T *>(dst);
                    const T *srcObj = reinterpret_cast<const T *>(src);
                    *dstObj         = *srcObj;
                };
            }
            else
            {
                assigner = [](void *, const void *) {
                    assert(false && "Copy assignment called for non-copyable type!");
                };
            }

            // ムーブ構築（CompactHoles / MoveEntityBetweenChunks で使う）。
            // dst は未初期化メモリなので operator= ではなく placement new + std::move
            mover = [](void *dst, void *src) {
                new (dst) T(std::move(*reinterpret_cast<T *>(src)));
            };
        }

        TypeID      id       = InvalidTypeID; // コンポーネント型を識別する ID
        size_t      typeSize = 0;             // 1個あたりのメモリサイズ（byte）
        Destructor  destructor;               // デストラクタ呼び出しを型消去で保持
        const char *name     = nullptr;       // デバッグ・シリアライズ用の型名

        using ConstructFunc        = void (*)(void *, const void *); // placement new
        using AssignFunc           = void (*)(void *, const void *); // operator=
        using MoveFunc             = void (*)(void *, void *);       // ムーブ構築
        using DefaultConstructFunc = void (*)(void *);               // デフォルト構築

        ConstructFunc        constructor        = nullptr;
        AssignFunc           assigner           = nullptr;
        MoveFunc             mover              = nullptr;
        DefaultConstructFunc defaultConstructor = nullptr;

        /**
         * @brief id で比較する。Archetype が TypeData の集合をソートして持つために使う。
         *
         * ソート順を一定にしておくと、コンポーネントの指定順が違っても
         * 同一 Archetype だと高速に判定できる。
         *
         *   A: [ Velocity(2), Position(1) ]  →ソート→ [Position(1), Velocity(2)]
         *   B: [ Position(1), Velocity(2) ]  →ソート→ [Position(1), Velocity(2)]
         *   A == B ⇒ true
         */
        bool operator<(const TypeData &r) const { return id < r.id; }
        bool operator==(const TypeData &r) const { return id == r.id; }
    };

} // namespace CCL::ECS
