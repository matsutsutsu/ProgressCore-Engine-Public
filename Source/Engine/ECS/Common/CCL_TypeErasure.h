#pragma once
// ===========================================================================
// 概要: 型情報が消えた生メモリ（void*）に対して、その型本来のデストラクタ・
//       コピーコンストラクタを呼ぶための型消去（Type Erasure）ユーティリティ
//
// 責務:
//   - Destructor: 型を消した状態でデストラクタを呼ぶ
//   - Copier:     型を消した状態でコピー構築する
//
// 備考: Chunk は SoA で複数型のコンポーネントを生メモリに持つので、その時点で
//       型情報は消えている。Entity 削除や Chunk 間移動で「正しい型」として
//       破棄・コピーできるよう、事前にやり方を関数ポインタで登録しておく
// ===========================================================================

#include <new>

namespace CCL::ECS
{
    // ============================================================
    // Destructor
    // ============================================================

    // 型を消した void* に対して、登録した型 T のデストラクタを呼ぶ
    class Destructor
    {
    public:
        Destructor()  = default;
        ~Destructor() = default;

        // 破棄する型 T を登録する
        template <class T>
        void Create()
        {
            destructFunc = &CallDestructorFromPtr<T>;
        }

        void Destruct(void *dataPtr) const
        {
            if (destructFunc) destructFunc(dataPtr);
        }

        // Chunk のメモリは placement new で作っているので、delete でなくデストラクタを直接呼ぶ
        template <class T>
        static void CallDestructorFromPtr(void *dataPtr)
        {
            if (dataPtr) reinterpret_cast<T *>(dataPtr)->~T();
        }

    private:
        void (*destructFunc)(void *) = nullptr; // 登録した型のデストラクタを呼ぶ関数
    };

    // ============================================================
    // Copier
    // ============================================================

    // 型を消した void* に対して、登録した型のコピー構築を呼ぶ
    class Copier
    {
    public:
        // この Copier がコピーを担当する型 T を登録する
        template <class T>
        void Create()
        {
            copyFunc = &CallCopyFromPtr<T>;
        }

        // 未初期化の dst に、src をコピー構築する
        void Copy(void *dst, const void *src) const
        {
            if (copyFunc) copyFunc(dst, src);
        }

    private:
        // dst に placement new で T を構築する
        template <class T>
        static void CallCopyFromPtr(void *dst, const void *src)
        {
            new (dst) T(*reinterpret_cast<const T *>(src));
        }

        void (*copyFunc)(void *, const void *) = nullptr;
    };

} // namespace CCL::ECS
