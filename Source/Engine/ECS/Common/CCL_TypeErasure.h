#pragma once
// ===========================================================================
// 概要: 型情報が消えた生メモリ（void*）に対して、その型本来のデストラクタ・
//       コピーコンストラクタを呼ぶための型消去（Type Erasure）ユーティリティ。
//
// 責務:
//   - Destructor: 型を消した状態でデストラクタを呼ぶ
//   - Copier:     型を消した状態でコピー構築する
//
// 備考: Chunk は SoA で複数型のコンポーネントを生メモリに持つので、その時点で
//       型情報は消えている。Entity 削除や Chunk 間移動で「正しい型」として
//       破棄・コピーできるよう、事前にやり方を関数ポインタで登録しておく。
// ===========================================================================

#include <new> // placement new

namespace CCL::ECS
{
    // ============================================================
    // Destructor
    // ============================================================

    /**
     * @brief 任意型 T のデストラクタを、型を消した状態で呼び出せるよう包んだ
     *        型消去ユーティリティ。
     *
     * 用途:
     *   - Chunk 内で void* として保持しているコンポーネントの破棄
     *   - PendingOp で型を保持できないコンポーネントの安全な解放
     *
     * 使い方:
     * @code
     *   Destructor d;
     *   d.Create<Velocity>();        // この Destructor は Velocity 担当
     *   d.Destruct(rawPointer);      // void* → Velocity* に戻して ~Velocity()
     * @endcode
     */
    class Destructor
    {
    public:
        Destructor()  = default;
        ~Destructor() = default;

        /**
         * @brief この Destructor が破棄を担当する型 T を登録する。
         *
         * 型 T 用のデストラクタ呼び出し関数（CallDestructorFromPtr<T>）を
         * 関数ポインタとして保存する。以降は型情報を失った void* に対しても、
         * Destruct() を呼ぶだけで本来のデストラクタが走る。
         */
        template <class T>
        void Create()
        {
            destructFunc = &CallDestructorFromPtr<T>;
        }

        /**
         * @brief 登録済みの破棄関数を実行する。
         * @param dataPtr 破棄したいオブジェクトへの生ポインタ
         */
        void Destruct(void *dataPtr) const
        {
            if (destructFunc) destructFunc(dataPtr);
        }

        /**
         * @brief void* を T* に復元して T のデストラクタを直接呼ぶ静的ヘルパー。
         *
         * Chunk のメモリは placement new で構築しているので、delete ではなく
         * 明示的なデストラクタ呼び出しで破棄する必要がある。
         */
        template <class T>
        static void CallDestructorFromPtr(void *dataPtr)
        {
            if (dataPtr) reinterpret_cast<T *>(dataPtr)->~T();
        }

    private:
        // 関数ポインタの読み方:
        //   void  (*destructFunc)(void*) = nullptr;
        //    └戻値  └ポインタ名         └引数リスト
        // → 「void* を1つ受け取って void を返す関数」を指すポインタ
        void (*destructFunc)(void *) = nullptr;
    };

    // ============================================================
    // Copier
    // ============================================================

    /**
     * @brief 型情報が消えた void* に対して、正しいコピーコンストラクタを
     *        呼び出すための型消去ユーティリティ。
     *
     * 主な用途:
     *   - Entity が別の Archetype に移動するときのデータ複製
     *   - Chunk が満杯になり別 Chunk へ詰め替えるときの複製
     *
     * いずれも「型情報は失われているが、データは保持したまま移したい」場面で使う。
     */
    class Copier
    {
    public:
        // この Copier がコピーを担当する型 T を登録する
        template <class T>
        void Create()
        {
            copyFunc = &CallCopyFromPtr<T>;
        }

        /**
         * @brief src の中身を dst に対してコピー構築する。
         * @param dst コピー先の未初期化メモリ
         * @param src コピー元のオブジェクト
         */
        void Copy(void *dst, const void *src) const
        {
            if (copyFunc) copyFunc(dst, src);
        }

    private:
        /**
         * @brief src を T* として読み、dst に placement new で T を構築する。
         *
         *   new (dst) T(*reinterpret_cast<const T*>(src));
         *    │            └─ void* を const T* に復元してコピー構築
         *    └─ placement new: 既に確保済みのメモリ位置に直接構築
         */
        template <class T>
        static void CallCopyFromPtr(void *dst, const void *src)
        {
            new (dst) T(*reinterpret_cast<const T *>(src));
        }

        void (*copyFunc)(void *, const void *) = nullptr;
    };

} // namespace CCL::ECS
