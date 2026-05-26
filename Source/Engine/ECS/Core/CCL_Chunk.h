/**
 * @file   CCL_Chunk.h
 * @brief  同一Archetypeに属するEntity群をSoA(Structure of Arrays)で格納するデータストア
 *
 * @details
 *  Chunkの役割:
 *   - 同じコンポーネント構成を持つEntityのみを集約
 *   - コンポーネント種別ごとに連続メモリ領域を確保(SoA)
 *   - CPUキャッシュ効率の良いイテレーションを可能にする
 *
 *  メモリレイアウト(単一メモリブロック内):
 *  @verbatim
 *    [ Position[0]  Position[1]  ... Position[capacity-1] ] ← 16byteアライン
 *    [ Velocity[0]  Velocity[1]  ... Velocity[capacity-1] ] ← 16byteアライン
 *    [ Renderable[0] ...                                  ]
 *  @endverbatim
 *
 *  【本ヘッダで適用済みのレビュー指摘】
 *   - IsEntityDestroyed の O(n) 検索 → ビット配列で O(1) に
 *   - Chunk自身が「自分のインデックス」を保持(ChunkManager側のO(n)検索を回避)
 *
 *  【依存】
 *   - CCL_Archetype.h
 *   - \<vector\>, \<functional\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "../Common/CCL_Archetype.h"

#include <vector>
#include <cstdint>
#include <cstddef>
#include <functional>


namespace CCL::ECS::Core
{
    using namespace CCL::ECS;

    // =========================================================================
    //  Chunk
    // =========================================================================

    /**
     * @class Chunk
     * @brief 同一Archetypeに属するEntity群のSoAストレージ
     *
     * @details
     *  Chunkは生成時に容量(capacity)が固定されます。
     *  満杯になった場合は ChunkManager が同一Archetypeの新Chunkを別途生成します。
     *
     *  @par 主要API
     *   - Create()           : 指定Archetypeで初期化
     *   - Finalize()         : 内部Entity全てを破棄
     *   - InsertEntity()     : Entity挿入(0初期化)
     *   - MarkDestroyEntity(): Entity削除予約(後でCompactHolesが穴を詰める)
     *   - CompactHoles()     : 削除予約済み穴を詰める
     *   - GetComponentArray\<T\>() : 型Tのコンポーネント配列の先頭ポインタ
     *
     *  @note コピー・ムーブともに禁止。`std::unique_ptr<Chunk>` で保持してください。
     */
    class Chunk
    {
    public:
        Chunk()  = default;
        ~Chunk();

        // コピー/ムーブ禁止
        Chunk(const Chunk&)            = delete;
        Chunk& operator=(const Chunk&) = delete;
        Chunk(Chunk&&)                 = delete;
        Chunk& operator=(Chunk&&)      = delete;


        // =====================================================================
        //  ライフサイクル
        // =====================================================================

        /**
         * @brief 指定Archetypeでメモリブロックを一括確保
         * @param archetype             保持するコンポーネント構成
         * @param initialEntityCapacity 確保するEntity収容数
         * @details 16byteアラインで各コンポーネント配列を配置します。
         */
        void Create(const Archetype& archetype, std::size_t initialEntityCapacity = 16);

        /**
         * @brief Archetype無しの空Chunkを生成(タグ専用Entity用)
         * @param initialEntityCapacity Entity収容数
         */
        void CreateNull(std::size_t initialEntityCapacity = 16);

        /**
         * @brief 全Entityのデストラクタを呼んでChunkを終了する
         * @details 既にFinalize済み(または未初期化)の場合は何もしません。
         */
        void Finalize();


        // =====================================================================
        //  Entity 操作
        // =====================================================================

        /**
         * @brief Entityをchunkに挿入し、全コンポーネントを0初期化する
         * @param entity 挿入するEntityID
         * @return       コンポーネント[0]の先頭ポインタ(便宜的に返す)
         * @warning Chunkが満杯の場合は assert で停止します。事前に GetEntityCount() を確認。
         */
        void* InsertEntity(EntityID entity);

        /**
         * @brief Entityをchunkに挿入し、内部インデックスを返す(初期化は呼び出し側で)
         * @param entity 挿入するEntityID
         * @return 挿入されたスロットのインデックス
         * @warning Chunkが満杯の場合は assert で停止し InvalidIndex を返します。
         */
        std::size_t AddEntity(EntityID entity);

        /**
         * @brief Entityを即時削除する(全コンポーネントのデストラクタを呼び、スロットを無効化)
         * @param entityIndex Chunk内インデックス
         */
        void RemoveEntity(std::size_t entityIndex);

        /**
         * @brief Entityを「後で削除する」と予約する(デストラクタは呼ばない)
         * @param entityIndex Chunk内インデックス
         * @details 実体データの破棄は CompactHoles() の前に外側で行う想定です。
         */
        void MarkDestroyEntity(std::size_t entityIndex);

        /**
         * @brief 削除予約された穴を詰めて連続メモリを保つ
         * @param onMoveCallback Entityが別スロットへ移動した際の通知コールバック
         *                       (EntityID, Chunk*, 新インデックス) を受け取る
         */
        void CompactHoles(const std::function<void(EntityID, Chunk*, std::size_t)>& onMoveCallback);


        // =====================================================================
        //  問い合わせ
        // =====================================================================

        /// @brief 現在格納されているEntity数(削除予約済みも含む)
        std::size_t      GetEntityCount() const noexcept { return _entityCount; }

        /// @brief このChunkに収容可能な最大Entity数
        std::size_t      GetCapacity()    const noexcept { return _capacity; }

        /// @brief 1Entity当たりのコンポーネント合計サイズ(byte)
        std::size_t      GetEntityStride() const noexcept { return _entityStride; }

        /// @brief このChunkのArchetype参照
        const Archetype& GetArchetype()   const noexcept { return _archetype; }

        /// @brief 指定インデックスのEntityIDを取得
        EntityID         GetEntityByIndex(std::size_t i) const { return _ownerEntities[i]; }

        /// @brief Entity ID配列の先頭ポインタ(ScriptSystem等で使用)
        const EntityID*  GetEntityIDs() const noexcept
        {
            return _ownerEntities.empty() ? nullptr : _ownerEntities.data();
        }

        /// @brief 削除予約があるか
        bool HasDestroyedEntities() const noexcept { return _destroyedCount > 0; }


        // =====================================================================
        //  ChunkManager 用: 自分のインデックスを保持(O(1)逆引き用)
        // =====================================================================

        /// @brief ChunkManagerが Chunk のインデックスを設定
        void        SetSelfIndex(std::size_t idx) noexcept { _selfIndex = idx; }

        /// @brief ChunkManager内での自分のインデックスを取得
        std::size_t GetSelfIndex() const noexcept          { return _selfIndex; }


        // =====================================================================
        //  削除フラグ操作 (O(1) 化済み)
        // =====================================================================

        /**
         * @brief 指定インデックスのEntityが削除予約されているか
         * @param index Chunk内インデックス
         * @return true なら削除予約済み
         * @note std::vector<uint8_t> によるビット配列で O(1) チェックです。
         */
        bool IsEntityDestroyed(std::size_t index) const noexcept
        {
            return index < _destroyedFlags.size() && _destroyedFlags[index] != 0;
        }


        // =====================================================================
        //  型情報アクセス
        // =====================================================================

        /// @brief TypeIDからArchetype内のインデックスを取得(なければInvalidIndex)
        std::size_t GetComponentIndex(TypeID id) const
        {
            return _archetype.GetComponentIndex(id);
        }

        /// @brief 指定TypeIDが格納されているか判定
        bool HasComponent(TypeID id) const { return GetComponentIndex(id) != InvalidIndex; }

        /// @brief 指定型Tが格納されているか判定
        template <class T>
        bool HasComponent() const { return HasComponent(TypeInfo<T>::ID()); }


        // =====================================================================
        //  生メモリアクセス
        // =====================================================================

        /**
         * @brief TypeIDとEntityインデックスから、コンポーネントの生ポインタを取得
         * @param id          コンポーネントのTypeID
         * @param entityIndex Chunk内のEntityインデックス
         * @return 該当データのvoid*ポインタ。型を持っていなければ nullptr。
         */
        void* GetComponentPtrByType(TypeID id, std::size_t entityIndex)
        {
            std::size_t compIdx = GetComponentIndex(id);
            if (compIdx == InvalidIndex) return nullptr;
            return GetComponentPtr(compIdx, entityIndex);
        }

        /**
         * @brief 型Tのコンポーネント配列(SoAの1列)の先頭ポインタを取得
         * @tparam T 取得したい型
         * @return T*ポインタ(存在しなければ nullptr)
         */
        template <class T>
        T* GetComponentArray()
        {
            std::size_t idx = GetComponentIndex(TypeInfo<T>::ID());
            if (idx == InvalidIndex) return nullptr;
            return reinterpret_cast<T*>(_memoryBlock.data() + _componentOffsets[idx]);
        }

        /// @brief const版 GetComponentArray
        template <class T>
        const T* GetComponentArray() const
        {
            std::size_t idx = GetComponentIndex(TypeInfo<T>::ID());
            if (idx == InvalidIndex) return nullptr;
            return reinterpret_cast<const T*>(_memoryBlock.data() + _componentOffsets[idx]);
        }

    private:
        // =====================================================================
        //  内部ヘルパ
        // =====================================================================

        /**
         * @brief 「コンポーネントインデックス」と「Entityインデックス」から生ポインタを計算
         * @param compIdx     Archetype内のコンポーネント位置
         * @param entityIndex Chunk内のEntity位置
         */
        void* GetComponentPtr(std::size_t compIdx, std::size_t entityIndex)
        {
            const std::size_t baseOffset = _componentOffsets[compIdx];
            const std::size_t stride     = _archetype[compIdx].typeSize;
            return _memoryBlock.data() + baseOffset + (entityIndex * stride);
        }


    private:
        // =====================================================================
        //  メンバ変数
        // =====================================================================

        Archetype                _archetype;            ///< このChunkのコンポーネント構成
        std::size_t              _capacity     = 0;     ///< 最大Entity収容数
        std::size_t              _entityCount  = 0;     ///< 現在の格納数(削除予約済み含む)
        std::size_t              _entityStride = 0;     ///< 1Entity当たりの合計バイト数

        std::vector<std::uint8_t> _memoryBlock;         ///< 全コンポーネントを1ブロックで保持
        std::vector<std::size_t>  _componentOffsets;    ///< 各コンポーネント配列の先頭オフセット

        std::vector<EntityID>    _ownerEntities;        ///< スロット番号 → EntityID
        std::vector<std::uint8_t> _destroyedFlags;      ///< スロットごとの削除予約フラグ(1byte/Entity)
        std::size_t              _destroyedCount = 0;   ///< 削除予約数(早期return用)

        std::size_t              _selfIndex = InvalidIndex; ///< ChunkManager内の自分のindex
    };

} // namespace CCL::ECS::Core
