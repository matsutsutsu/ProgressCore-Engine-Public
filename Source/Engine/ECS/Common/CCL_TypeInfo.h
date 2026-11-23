#pragma once
// ===========================================================================
// 概要: 任意の型 T の「型ID・サイズ・型名・構築/破棄関数」を提供する型情報レイヤー
//
// 責務:
//   - TypeInfo<T>: 型 T → 数値 ID の変換（コンパイル時テンプレート）
//   - TypeData:    実行時に持ち回る型情報レコード（id / size / 関数ポインタ）
// ===========================================================================

#include <cstddef>
#include <string>
#include <new>
#include <type_traits>
#include <typeinfo>
#include <cassert>
#include <utility>

#include "CCL_Types.h"
#include "CCL_IDEmitter.h"
#include "CCL_TypeErasure.h"

namespace CCL::ECS
{
    // ============================================================
    // TypeInfo<T>
    // ============================================================

    // コンポーネントの型 T に、一意な数値の ID を割り当てる。Archetype は型でなくこの ID で比べる
    template <class T>
    class TypeInfo
    {
    public:
        // T の ID を返す。採番は関数ローカルの static で行い、静的初期化の途中で呼ばれても正しい値を返す
        // inline static のメンバにすると、翻訳単位をまたぐ初期化の順が決まらず、REGISTER_COMPONENT が 0 を読む
        // すると型と名前の対応が 0 番に化け、保存したときに特定のコンポーネントが黙って消える
        static TypeID ID()
        {
            static const TypeID s_id = ID_Emitter::EmitTypeID();
            return s_id;
        }

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

    // 型 T の ID・大きさ・構築と破棄の関数を、実行時に持ち回れる形にまとめたもの
    // Chunk は型を知らないので、コンポーネントをこの関数ポインタで作り、運び、壊す
    class TypeData
    {
    public:
        TypeData()  = default;
        ~TypeData() = default;

        // vector 等に格納するため、コピー・ムーブはすべて有効
        TypeData(const TypeData &)            = default;
        TypeData &operator=(const TypeData &) = default;
        TypeData(TypeData &&)                 = default;
        TypeData &operator=(TypeData &&)      = default;

        // T から各欄を埋める。コピーできない型でもコンパイルが通るよう if constexpr で分け、
        // そうした型のコピーが実行時に呼ばれたら assert で止める
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

            // コピー構築（placement new）
            // 未初期化メモリへの新規構築。assigner と違って operator= は呼ばない
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

            // ムーブ構築（CompactHoles / MoveEntityBetweenChunks で使う）
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

        // id で並べる。Archetype は TypeData を id の順に持ち、指定した順によらず同じ構成を一致させる
        bool operator<(const TypeData &r) const { return id < r.id; }
        bool operator==(const TypeData &r) const { return id == r.id; }
    };

}
