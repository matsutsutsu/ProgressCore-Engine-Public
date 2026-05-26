/**
 * @file   CCL_Destructor.h
 * @brief  型情報が消えた生メモリに対し、正しいデストラクタやコピーを呼ぶための装置
 *
 * @details
 *  ECSではコンポーネントを `std::vector<uint8_t>` などの「型情報なしのバイト列」
 *  として格納します。そのまま `void*` を delete するとUBになるため、
 *  型を覚えている関数ポインタを通して破棄処理を行います。
 *
 *  本ファイルでは以下を提供します:
 *   - Destructor : 型Tのデストラクタを後から呼べるよう保存する装置
 *   - Copier     : 型Tのコピー構築を後から呼べるよう保存する装置
 *
 *  これらは TypeData::Create\<T\>() から組み立てられます。
 *  実際のECS実装では関数ポインタやラムダ経由で型操作を行うため、
 *  void* を介した型安全な扱いの基盤となります。
 *
 *  【依存】
 *   - \<new\> (placement new 用)
 *   依存はこれだけです。テンプレ重量が軽いため広範囲に include 可能。
 *
 * @author CCL ECS Library
 */
#pragma once

#include <new>

namespace CCL::ECS
{
    // =========================================================================
    //  Destructor
    // =========================================================================

    /**
     * @class Destructor
     * @brief 型Tのデストラクタを保存し、型情報なしの void* に対して呼び出す装置
     *
     * @details
     *  使用例:
     *  @code
     *      Destructor d;
     *      d.Create<MyComponent>();      // 型Tのデストラクタを記憶
     *      d.Destruct(rawPtr);           // void*に対して ~MyComponent() を呼ぶ
     *  @endcode
     *
     *  関数ポインタ1個 (8byte) しか持たないため、TypeData等に値で持っても軽量です。
     *
     * @note Destruct() に nullptr を渡しても安全(何もしません)。
     */
    class Destructor
    {
    public:
        Destructor() = default;
        ~Destructor() = default;

        /**
         * @brief 型Tのデストラクタを記憶する
         * @tparam T 後で破棄したいオブジェクトの型
         */
        template <class T>
        void Create() noexcept
        {
            _destructFunc = &CallDestructorFromPtr<T>;
        }

        /**
         * @brief 記憶した型のデストラクタを呼び出す
         * @param dataPtr  破棄したいオブジェクトへのポインタ(void*でOK)
         * @details dataPtrがnullptrの場合、または Create() 未実行の場合は何もしません。
         */
        void Destruct(void* dataPtr) const noexcept
        {
            if (_destructFunc) _destructFunc(dataPtr);
        }

        /**
         * @brief 内部使用: 型Tのデストラクタを直接呼ぶラッパー関数
         * @tparam T 型
         * @param  dataPtr 対象ポインタ
         */
        template <class T>
        static void CallDestructorFromPtr(void* dataPtr) noexcept
        {
            if (dataPtr)
            {
                // void*をT*へキャストして本物のデストラクタを呼ぶ
                reinterpret_cast<T*>(dataPtr)->~T();
            }
        }

    private:
        /// @brief デストラクタを呼ぶ関数ポインタ (void(void*) 型)
        void (*_destructFunc)(void*) = nullptr;
    };


    // =========================================================================
    //  Copier
    // =========================================================================

    /**
     * @class Copier
     * @brief 型Tのコピーコンストラクタを保存し、生メモリ間でコピー構築を行う装置
     *
     * @details
     *  ECSではChunk間のEntityデータ移動が発生します。
     *  そのとき型を忘れた状態でコピー構築する必要があるため、
     *  ここに「正しいコピー方法」を覚えさせておきます。
     *
     *  使用例:
     *  @code
     *      Copier c;
     *      c.Create<MyComponent>();      // 型Tのコピー構築を記憶
     *      c.Copy(dst, src);             // void*間でコピー構築
     *  @endcode
     *
     * @note 現在は TypeData::constructor (関数ポインタ) が同等の機能を持つため、
     *       基本的にはそちらが使われます。Copierは予備的存在です。
     */
    class Copier
    {
    public:
        Copier() = default;
        ~Copier() = default;

        /**
         * @brief 型Tのコピーコンストラクタを記憶
         * @tparam T 対象型(コピー構築可能であること)
         */
        template <class T>
        void Create() noexcept
        {
            _copyFunc = &CallCopyFromPtr<T>;
        }

        /**
         * @brief 記憶した型のコピー構築を、生メモリに対して実行
         * @param dst コピー先(未初期化メモリ)
         * @param src コピー元
         */
        void Copy(void* dst, const void* src) const
        {
            if (_copyFunc) _copyFunc(dst, src);
        }

    private:
        /// @brief コピー構築を行う関数ポインタ
        void (*_copyFunc)(void*, const void*) = nullptr;

        /**
         * @brief 内部使用: srcにあるTのコピーをdstに placement new で構築
         */
        template <class T>
        static void CallCopyFromPtr(void* dst, const void* src)
        {
            new (dst) T(*reinterpret_cast<const T*>(src));
        }
    };

} // namespace CCL::ECS
