#pragma once
// ===========================================================================
// CCL_TypeInfo.h
// --------------
// 任意の型 T について「型ID・型サイズ・型名・各種コンストラクタ呼出関数」
// を提供する、ECS の型情報レイヤーです。
//
// ・TypeInfo<T> : コンパイル時テンプレート。型 T → 数値 ID への変換装置。
// ・TypeData    : 実行時に持ち回るための型情報レコード（id/size/関数ポインタ）。
// ===========================================================================

#include <cstddef>
#include <string>
#include <new>           // placement new
#include <type_traits>   // std::is_copy_constructible_v 等
#include <typeinfo>
#include <cassert>
#include <utility>       // std::move

#include "CCL_Types.h"
#include "CCL_IDEmitter.h"
#include "CCL_TypeErasure.h"


namespace CCL::ECS
{
    // ------------------------------------------------------------------
    // TypeInfo<T>
    // ------------------------------------------------------------------

    /**
     * @brief 任意のコンポーネント型 T に「ユニークな数値 ID」を割り当てる
     *        テンプレートユーティリティ。
     *
     * 型を直接比較するのではなく数値 ID で比較することで、Archetype の
     * 一致判定やコンポーネント配置オフセット計算を最速化します。
     *
     * 仕組み:
     *   `inline static const TypeID s_id = ID_Emitter::EmitTypeID();`
     *   C++17 の inline static 変数機能により、複数の .cpp に散らばっても
     *   リンカが必ず単一インスタンスにマージします。結果として、ある型 T に
     *   ついて s_id が初期化されるのは「プロセス全体で1度きり」となり、
     *   100% 衝突しないユニーク ID が手に入ります。
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
         * @brief 型 T に紐づく永続ユニーク ID。
         * @note  inline static により全翻訳単位で同一実体になる。
         */
        inline static const TypeID s_id = ID_Emitter::EmitTypeID();

        /// @brief 型 T の TypeID を返す（ただの変数読み込みなので超高速）
        static TypeID ID()
        {
            return s_id;
        }

        /**
         * @brief sizeof(T) を返す。
         *
         * Chunk が SoA 配置の計算をする際に必須
         * （ 先頭アドレス + entityIndex * Size() で各エンティティの番地が決まる）。
         */
        static size_t Size()
        {
            return sizeof(T);
        }

        /// @brief 型名（実装依存。デバッグ・シリアライズで使用）
        static const char* Name()
        {
            static std::string name = typeid(T).name(); // "struct Transform" 等
            return name.c_str();
        }
    };


    // ------------------------------------------------------------------
    // TypeData
    // ------------------------------------------------------------------

    /**
     * @brief 任意のコンポーネント型 T を「実行時に持ち回れる情報」として
     *        パッケージ化したレコード。
     *
     * TypeInfo<T> がコンパイル時テンプレートなのに対し、TypeData は
     * 実行時に std::vector に格納したり、関数引数として渡したりするための
     * 「型情報の運び屋」です。
     *
     * 含まれる情報:
     *   - id               : コンポーネント型を識別するID（== 比較が最速）
     *   - typeSize         : 1個あたりのメモリサイズ
     *   - destructor       : デストラクタ呼出を型消去で持つ
     *   - constructor      : placement new によるコピー構築
     *   - assigner         : operator= による代入
     *   - mover            : ムーブ構築 (placement new + std::move)
     *   - defaultConstructor: デフォルトコンストラクタ呼出
     *   - name             : デバッグ用の型名
     *
     * @note ECS が各コンポーネント型を「メモリ操作可能なデータ」として
     *       一律に扱うためのインターフェースであり、ECS の中核データ構造です。
     */
    class TypeData
    {
    public:
        TypeData()  = default;
        ~TypeData() = default;

        // コピー・ムーブは全て有効化（vector 等に格納するため）
        TypeData(const TypeData&)            = default;
        TypeData& operator=(const TypeData&) = default;
        TypeData(TypeData&&)                 = default;
        TypeData& operator=(TypeData&&)      = default;


        /**
         * @brief 型 T から TypeData の各フィールドを一括初期化する。
         *
         * id / size / 各種コンストラクタ呼出関数（コピー・ムーブ・代入・
         * デフォルト構築）と、デストラクタを一気に登録します。
         *
         * @note コピーやコピー代入が禁止された型でも安全に扱えるよう、
         *       `if constexpr` で コピー可能性をコンパイル時判定しています。
         *       禁止型を間違って呼んだ場合は assert が走ります。
         */
        template <class T>
        void Create()
        {
            id       = TypeInfo<T>::ID();
            typeSize = TypeInfo<T>::Size();
            destructor.template Create<T>();
            name     = TypeInfo<T>::Name();

            // ---- デフォルトコンストラクタ ----
            defaultConstructor = [](void* dst) {
                new (dst) T();
            };

            // ---- コピー構築 (placement new) ----
            // 未初期化メモリへの新規構築。assigner と違って operator= は呼ばない。
            if constexpr (std::is_copy_constructible_v<T>) {
                constructor = [](void* dst, const void* src) {
                    new (dst) T(*reinterpret_cast<const T*>(src));
                };
            }
            else {
                constructor = [](void*, const void*) {
                    assert(false && "Copy constructor called for non-copyable type!");
                };
            }

            // ---- コピー代入 (operator=) ----
            if constexpr (std::is_copy_assignable_v<T>) {
                assigner = [](void* dst, const void* src) {
                    T*       dstObj = reinterpret_cast<T*>(dst);
                    const T* srcObj = reinterpret_cast<const T*>(src);
                    *dstObj = *srcObj;
                };
            }
            else {
                assigner = [](void*, const void*) {
                    assert(false && "Copy assignment called for non-copyable type!");
                };
            }

            // ---- ムーブ構築 (CompactHoles / MoveEntityBetweenChunks で使用) ----
            // dst は未初期化メモリなので operator= ではなく placement new + std::move
            mover = [](void* dst, void* src) {
                new (dst) T(std::move(*reinterpret_cast<T*>(src)));
            };
        }


        // ----------------------------------------------------------
        // データメンバ
        // ----------------------------------------------------------
        /// @brief コンポーネント型を識別する ID（整数比較なので最速）
        TypeID     id        = InvalidTypeID;

        /// @brief 1個あたりのメモリサイズ（SoA 配置・コピー計算に使用）
        size_t     typeSize  = 0;

        /// @brief デストラクタ呼出を型消去で保持
        Destructor destructor;

        /// @brief デバッグ・シリアライズ用の型名
        const char* name     = nullptr;


        // ----------------------------------------------------------
        // 関数ポインタ型エイリアス
        // ----------------------------------------------------------
        using ConstructFunc        = void(*)(void*, const void*); ///< placement new
        using AssignFunc           = void(*)(void*, const void*); ///< operator=
        using MoveFunc             = void(*)(void*, void*);       ///< ムーブ構築
        using DefaultConstructFunc = void(*)(void*);              ///< デフォルト構築

        ConstructFunc        constructor        = nullptr;
        AssignFunc           assigner           = nullptr;
        MoveFunc             mover              = nullptr;
        DefaultConstructFunc defaultConstructor = nullptr;


        // ----------------------------------------------------------
        // 比較演算子
        // ----------------------------------------------------------
        //
        // Archetype は TypeData の集合をソートして保持します。
        // ソート順を一定にすることで、コンポーネント順序が違っても
        // 同一 Archetype として高速に判定できるようになります。
        //
        //   例:
        //     A: [ Velocity(2), Position(1) ]  →ソート→ [Position(1), Velocity(2)]
        //     B: [ Position(1), Velocity(2) ]  →ソート→ [Position(1), Velocity(2)]
        //     A == B ⇒ true
        // ----------------------------------------------------------
        bool operator<(const TypeData& r)  const { return id < r.id; }
        bool operator==(const TypeData& r) const { return id == r.id; }
    };

} // namespace CCL::ECS
