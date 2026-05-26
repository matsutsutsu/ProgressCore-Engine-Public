/**
 * @file   CCL_IDs.h
 * @brief  ECS基盤の最小ID型・定数・ID発行機構の定義
 *
 * @details
 *  このヘッダはECSライブラリ全体の最も底辺に位置するファイルです。
 *  以下を提供します:
 *   - 各種ID型エイリアス (EntityID, TypeID, ComponentID, SystemID)
 *   - 無効値を表すconstexpr定数 (InvalidEntityIDなど)
 *   - スレッドセーフなID発行クラス (ID_Emitter)
 *   - 世代番号付きEntityIDの分解・合成ヘルパ (EntityHandle)
 *
 *  【設計方針】
 *  このヘッダはあらゆるECSファイルから include されるため、依存STLを最小限
 *  (\<cstdint\>, \<cstddef\>, \<atomic\>) に抑え、ビルド速度への悪影響を防ぎます。
 *  \<vector\>, \<mutex\>, \<algorithm\> などはここには含めません。
 *
 *  【スレッドセーフ性】
 *  ID_Emitterの内部カウンタはstd::atomicで保護されているため、
 *  複数スレッドから安全にID発行可能です。
 *
 * @author CCL ECS Library
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>

namespace CCL::ECS
{
    // =========================================================================
    //  基本ID型エイリアス
    // =========================================================================

    /**
     * @brief エンティティを一意に識別する64bit ID
     * @details 上位32bitに世代番号、下位32bitにスロット番号を格納します。
     *          世代番号により「使い回されたID」の誤検出を防ぎます。
     */
    using EntityID = std::uint64_t;

    /**
     * @brief コンポーネント型を識別する32bit ID
     * @details TypeInfo<T>::ID() により型ごとに一意な値が振られます。
     *          unsigned shortから引き上げて 65535型制限を撤廃しました。
     */
    using TypeID = std::uint32_t;

    /// @brief コンポーネントインスタンスを識別するID(将来拡張用)
    using ComponentID = std::uint64_t;

    /// @brief Systemを識別するID
    using SystemID = std::uint32_t;


    // =========================================================================
    //  無効値定数
    // =========================================================================

    /// @brief 無効なEntityIDを示す定数
    inline constexpr EntityID    InvalidEntityID    = static_cast<EntityID>(-1);

    /// @brief 無効なTypeIDを示す定数
    inline constexpr TypeID      InvalidTypeID      = static_cast<TypeID>(-1);

    /// @brief 無効なComponentIDを示す定数
    inline constexpr ComponentID InvalidComponentID = static_cast<ComponentID>(-1);

    /// @brief 無効なSystemIDを示す定数
    inline constexpr SystemID    InvalidSystemID    = static_cast<SystemID>(-1);

    /// @brief 無効なインデックスを示す定数(size_t用)
    inline constexpr std::size_t InvalidIndex       = static_cast<std::size_t>(-1);


    // =========================================================================
    //  ID発行機構
    // =========================================================================

    /**
     * @class ID_Emitter
     * @brief 各種IDを連番で発行する機構
     *
     * @details
     *  各カウンタは std::atomic で実装されているため、
     *  複数スレッドから同時にIDを取得しても番号の衝突は起きません。
     *
     *  内部カウンタは関数ローカルstaticで宣言されており、
     *  プログラム全体で一意の値を保証します(SIOF回避済)。
     *
     * @note  EntityIDの直接発行はWorld経由を推奨します。
     *        Worldは世代番号付きで発行するため、ここではindexのみを発行します。
     */
    class ID_Emitter
    {
    public:
        /// @brief 新しいTypeID(コンポーネント型用)を発行
        static TypeID EmitTypeID() noexcept
        {
            static std::atomic<TypeID> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        /// @brief 新しいSystemIDを発行
        static SystemID EmitSystemID() noexcept
        {
            static std::atomic<SystemID> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        /// @brief 新しいComponentIDを発行(予備)
        static ComponentID EmitComponentID() noexcept
        {
            static std::atomic<ComponentID> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }
    };


    // =========================================================================
    //  EntityID 分解・合成ヘルパ
    // =========================================================================

    /**
     * @struct EntityHandle
     * @brief  EntityID(64bit)を「スロット番号」と「世代番号」に分解/合成する静的ヘルパ
     *
     * @details
     *  EntityIDのビットレイアウト:
     *    [63 ... 32] : 世代番号 (Generation, 32bit)
     *    [31 ...  0] : スロット番号 (Index, 32bit)
     *
     *  「同じスロットを再利用したエンティティ」を別物として識別するために、
     *  再利用のたびに世代番号がインクリメントされます。
     *  古いEntityIDで参照しても世代が一致しないため、安全に弾けます。
     */
    struct EntityHandle
    {
        /// @brief スロット番号を取り出すためのマスク
        static constexpr EntityID INDEX_MASK = 0xFFFF'FFFFULL;

        /// @brief 世代番号を取り出すためのシフト量
        static constexpr int GEN_SHIFT = 32;

        /**
         * @brief EntityIDからスロット番号を取り出す
         * @param id 対象EntityID
         * @return  スロット番号(32bit)
         */
        static constexpr std::uint32_t GetIndex(EntityID id) noexcept
        {
            return static_cast<std::uint32_t>(id & INDEX_MASK);
        }

        /**
         * @brief EntityIDから世代番号を取り出す
         * @param id 対象EntityID
         * @return  世代番号(32bit)
         */
        static constexpr std::uint32_t GetGeneration(EntityID id) noexcept
        {
            return static_cast<std::uint32_t>(id >> GEN_SHIFT);
        }

        /**
         * @brief スロット番号と世代番号からEntityIDを合成
         * @param index      スロット番号
         * @param generation 世代番号
         * @return           合成されたEntityID
         */
        static constexpr EntityID Combine(std::uint32_t index, std::uint32_t generation) noexcept
        {
            return (static_cast<EntityID>(generation) << GEN_SHIFT)
                 | static_cast<EntityID>(index);
        }
    };

} // namespace CCL::ECS
