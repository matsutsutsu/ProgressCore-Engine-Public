/**
 * @file   CCL_ChunkManager.h
 * @brief  Archetypeごとに作られたChunk群を統括し、Entityの配置・遷移・破棄を管理する
 *
 * @details
 *  ChunkManagerはECSの「住宅地」のように、Archetypeごとに建てられた
 *  Chunk(倉庫)を保有します。Entityを正しいChunkに割り当て、コンポーネント
 *  追加削除によるArchetype遷移をしたら別Chunkへ引越しさせます。
 *
 *  @par 主要API
 *   - CreateChunk        : Archetypeに対応するChunkを新規作成
 *   - FinalizeAll        : 全Chunkを解体
 *   - ApplyPendingOperations : PendingOpsをまとめて適用
 *   - SearchEntityIn     : EntityIDからChunkとインデックスを取得 (O(1))
 *
 *  【スレッドセーフ性】
 *  std::shared_mutex で構造変更を保護:
 *   - System実行中 (読み取り) : shared_lock (並列OK)
 *   - 構造変更 (書き込み)     : unique_lock (排他)
 *
 *  【依存】
 *   - CCL_Chunk.h, CCL_PendingOps.h
 *   - \<memory\>, \<vector\>, \<shared_mutex\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_Chunk.h"
#include "CCL_PendingOps.h"

#include <memory>
#include <vector>
#include <functional>
#include <shared_mutex>


namespace CCL::ECS::Core
{
    using namespace CCL::ECS;

    /**
     * @class ChunkManager
     * @brief Chunk群の所有・検索・構造変更を担う
     */
    class ChunkManager
    {
    public:
        ChunkManager()  = default;
        ~ChunkManager() = default;

        ChunkManager(const ChunkManager&)            = delete;
        ChunkManager& operator=(const ChunkManager&) = delete;


        // =====================================================================
        //  Chunk生成/破棄
        // =====================================================================

        /**
         * @brief 指定Archetype用のChunkを必要なら新規作成
         * @param archetype 作成したい構成
         * @note 同じArchetypeのChunkが既にあれば何もしません
         */
        void CreateChunk(const Archetype& archetype);

        /**
         * @brief 全Chunkを解体(全Entityのデストラクタを呼ぶ)
         */
        void FinalizeAll();


        // =====================================================================
        //  Entity 直接操作 (主にPendingOps経由を推奨)
        // =====================================================================

        /**
         * @brief 既に発行されたEntityIDをChunkへ挿入
         * @param entity     EntityID
         * @param archetype  挿入先のArchetype
         */
        void AddEntity(EntityID entity, const Archetype& archetype);

        /**
         * @brief Entityを削除(デストラクタを呼んでスロットを無効化)
         * @param entity EntityID
         */
        void DestroyEntity(EntityID entity);


        // =====================================================================
        //  検索
        // =====================================================================

        /**
         * @brief EntityIDからChunkインデックスとEntity内インデックスを取得 (O(1))
         * @param entity         EntityID
         * @param outEntityIndex (out) Chunk内インデックス
         * @return Chunk配列内のインデックス。存在しなければ InvalidIndex
         */
        std::size_t SearchEntityIn(EntityID entity, std::size_t* outEntityIndex = nullptr) const;

        /**
         * @brief 指定Archetypeと完全一致するChunkを検索 (O(N))
         * @return Chunkインデックス。なければ InvalidIndex
         */
        std::size_t SearchEqualChunk(const Archetype& a) const;


        // =====================================================================
        //  まとめ適用
        // =====================================================================

        /**
         * @brief 蓄積されたPendingOpsを一括適用する
         * @param pendingOps 適用対象
         * @return 今回実際に削除されたEntityIDのリスト(World側のID再利用に使う)
         */
        std::vector<EntityID> ApplyPendingOperations(PendingOps& pendingOps);


        // =====================================================================
        //  アクセサ
        // =====================================================================

        std::vector<std::unique_ptr<Chunk>>&       GetChunks()       { return _chunks; }
        const std::vector<std::unique_ptr<Chunk>>& GetChunks() const { return _chunks; }

        /**
         * @brief Entityが別Chunkへ移動した際の通知コールバック型
         * @details (EntityID, Chunk*, 新インデックス) を受け取る
         */
        using EntityMoveCallback = std::function<void(EntityID, Chunk*, std::size_t)>;

        /// @brief Entity移動通知のコールバックを設定
        void SetEntityMoveCallback(EntityMoveCallback cb) { _onEntityMove = std::move(cb); }


        // =====================================================================
        //  共有Mutex (System並列実行のための読み取りロック取得用)
        // =====================================================================

        std::shared_mutex&       GetStructureMutex()       { return _structureMutex; }
        const std::shared_mutex& GetStructureMutex() const { return _structureMutex; }


    private:
        // =====================================================================
        //  Entity位置マップ用構造体
        // =====================================================================

        /**
         * @struct EntityLocation
         * @brief EntityID→Chunk位置 のキャッシュエントリ
         */
        struct EntityLocation
        {
            std::size_t chunkIndex  = InvalidIndex;
            std::size_t entityIndex = InvalidIndex;

            EntityLocation() = default;
            EntityLocation(std::size_t ci, std::size_t ei) : chunkIndex(ci), entityIndex(ei) {}
        };


        // =====================================================================
        //  内部ヘルパ
        // =====================================================================

        /// @brief 空きがあるArchetype対応Chunkを取得(なければ新規作成)
        Chunk& GetOrCreateAvailableChunk(const Archetype& archetype);

        /// @brief 新Entityを挿入し、各コンポーネントを初期化
        void InsertNewEntity(Chunk& dstChunk, EntityID entity,
                             const std::vector<PendingOp*>& addOps);

        /**
         * @brief Archetype遷移を伴うEntity引越し
         * @param srcChunk      元Chunk
         * @param srcIndex      元Chunkでのインデックス
         * @param dstChunk      新Chunk
         * @param entity        EntityID
         * @param addOps        追加される初期データ群
         * @param removeTypeIDs 削除される型ID群
         * @return 成功 true / 失敗 false
         */
        bool MoveEntityBetweenChunks(
            Chunk& srcChunk, std::size_t srcIndex,
            Chunk& dstChunk, EntityID entity,
            const std::vector<PendingOp*>& addOps,
            const std::vector<TypeID>& removeTypeIDs);


        // =====================================================================
        //  Entity位置キャッシュ操作
        // =====================================================================

        void UpdateEntityLocation(EntityID entity, std::size_t chunkIdx, std::size_t entityIdx);
        void RemoveEntityLocation(EntityID entity);

        /// @brief ChunkインデックスをChunkポインタから取得 (O(1): Chunk::GetSelfIndex使用)
        std::size_t GetChunkIndex(const Chunk* chunk) const;


        // =====================================================================
        //  Chunk容量計算
        // =====================================================================

        /// @brief Archetypeに対する Chunk1つの収容Entity数を計算
        std::size_t CalculateChunkCapacity(const Archetype& archetype) const;


    private:
        // =====================================================================
        //  メンバ
        // =====================================================================

        std::vector<std::unique_ptr<Chunk>> _chunks; ///< 全Chunk

        /**
         * @brief Chunk1つあたりの目標バイト数(16KB)
         * @details L1/L2キャッシュに収まりやすい標準的なサイズ。
         */
        static constexpr std::size_t CHUNK_SIZE_BYTES = 16 * 1024;

        EntityMoveCallback _onEntityMove = nullptr; ///< Entity移動通知

        std::vector<EntityLocation> _entityLocationMap; ///< EntityID(index)→位置 のキャッシュ

        /**
         * @brief 構造変更用の共有Mutex
         * @details 読み(System): shared_lock, 書き(変更適用): unique_lock
         */
        mutable std::shared_mutex _structureMutex;
    };

} // namespace CCL::ECS::Core
