#pragma once
#include <vector>
#include <memory>
#include "Engine/ECS/Common/CCL_Types.h"
#include "CCL_Chunk.h"
#include "CCL_PendingOps.h"
#include <functional>
#include <unordered_map> 
#include <unordered_set>
#include <shared_mutex>

namespace CCL::ECS
{
    using namespace CCL::ECS;

    // ----------------------------------------------------------------------
    // ChunkManager の役割（概要）
    // ----------------------------------------------------------------------
    // ECS において、エンティティはコンポーネントが追加・削除されるたびに
    // その構成（アーキタイプ）が変わります。ChunkManager の最大の仕事は、
    // 「エンティティをその時々の構成に最適な Chunk（倉庫）へ振り分け、
    //  移動させること」 です。
    // ----------------------------------------------------------------------

    /**
     * @brief 複数の Chunk を統括し、Entity の生成・破棄・Archetype 遷移を
     *        司る ECS の中核管理クラス。
     */
    class ChunkManager
    {
    public:
        ChunkManager() = default;
        ~ChunkManager() = default;

        /// @brief 指定 Archetype 用の新しい Chunk を作成して保持リストに追加
        void CreateChunk(const Archetype& archetype);

        /// @brief 全 Chunk に Finalize を呼び、全 Entity とデータを破棄
        void FinalizeAll();

        /**
         * @brief 新しいエンティティを生成し、指定 Archetype に対応する Chunk
         *        へ挿入する。
         */
        void AddEntity(EntityID entity, const Archetype& archetype);

        /// @brief 指定 Entity を破棄リストに入れる（即時実行）
        void DestroyEntity(EntityID entity);

        /**
         * @brief PendingOps を一括処理し、構造変更（Spawn/Destroy/Add/Remove
         *        /Patch）を全て適用する。
         *
         * @param pendingOps 適用対象の操作キュー
         * @return 実際に削除された Entity の ID 配列
         *
         * @note 本関数の内部は責務ごとに private 関数に分割されています:
         *       - GroupOpsByEntity        : 操作の Entity 別グルーピング
         *       - ProcessEntityOps        : 1 Entity 分の処理オーケストレーション
         *       - HasDestroyOp            : Destroy マーカー検出
         *       - ApplyDestroyOp          : Destroy 確定処理
         *       - DetermineNextArchetype  : 次 Archetype 決定（Spawn/既存）
         *       - CalculateArchetypeDiff  : Add/Remove 差分計算
         *       - UpdateExistingComponents: 同一 Archetype 内更新
         *       - ApplyPatchOps           : Patch 確定処理
         *       - CompactAllChunks        : 全 Chunk の穴詰め
         */
        std::vector<EntityID> ApplyPendingOperations(PendingOps& pendingOps);


        /**
         * @brief Archetype 遷移の中核ロジック。
         *
         * src チャンクから Entity のデータをコピー → デストラクタを呼び、
         * 新しいチャンクに（必要なら）コピー構築して移動を完了する。
         *
         * 引っ越し手順:
         *   1. 今の倉庫からデータを取り出す
         *   2. 新しい倉庫に席を確保
         *   3. 共通するデータだけを新しい倉庫にコピー（または move）
         *   4. 新規追加されるデータを書き込む
         *   5. 古い倉庫の席を空ける（削除マーク）
         */
        bool MoveEntityBetweenChunks(
            Chunk& srcChunk, size_t srcIndex,
            Chunk& dstChunk, EntityID entity,
            const std::vector<PendingOp*>& addOps,
            const std::vector<TypeID>& removeTypeIDs
        );

        /// @brief 指定 Archetype と完全一致する Chunk のインデックスを返す
        size_t SearchEqualChunk(const Archetype& a) const;

        std::vector<std::unique_ptr<Chunk>>& GetChunks() { return m_chunks; }
        const std::vector<std::unique_ptr<Chunk>>& GetChunks() const { return m_chunks; }

        /// @brief 指定 EntityID がどの Chunk のどのインデックスにあるかを検索
        size_t SearchEntityIn(EntityID entity, size_t* entityIndexOut = nullptr) const;

        /// @brief Entity 移動時の通知コールバック型
        using EntityMoveCallback = std::function<void(EntityID, Chunk*, size_t)>;
        void SetEntityMoveCallback(EntityMoveCallback cb) { m_onEntityMove = cb; }

        /// @brief 構造変更用 mutex（System 側の shared_lock 用）
        std::shared_mutex&       GetStructureMutex()       { return m_structureMutex; }
        const std::shared_mutex& GetStructureMutex() const { return m_structureMutex; }


    private:
        // ----------------------------------------------------------
        // Chunk 操作系ヘルパー
        // ----------------------------------------------------------

        /// @brief 指定 Archetype で空きがある Chunk を返す（無ければ作成）
        Chunk& GetOrCreateAvailableChunk(const Archetype& archetype);

        /**
         * @brief 新規 Entity を指定 Chunk に挿入し、コンポーネントデータを
         *        初期化する（Spawn 経路で使用）。
         */
        void InsertNewEntity(
            Chunk& dstChunk,
            EntityID entity,
            const std::vector<PendingOp*>& addOps
        );


        // ----------------------------------------------------------
        // ApplyPendingOperations の責務分割関数群
        // ----------------------------------------------------------
        // 本来は ApplyPendingOperations 1関数で行っていた処理を、
        // 役割ごとに分離したものです。挙動は元と完全に同一です。
        // ----------------------------------------------------------

        /**
         * @brief PendingOps を Entity ID ごとに束ねたマップに変換する。
         *
         * @code
         *   入力 (フラット):
         *     [Entity10 AddTransform]
         *     [Entity20 RemovePhysics]
         *     [Entity10 AddModel]
         *     [Entity30 Spawn]
         *
         *   出力 (Entity 単位):
         *     Entity10 → [AddTransform, AddModel]
         *     Entity20 → [RemovePhysics]
         *     Entity30 → [Spawn]
         * @endcode
         *
         * これにより、1 Entity の最終構成を一気に計算できるようになります。
         */
        std::unordered_map<EntityID, std::vector<PendingOp*>>
        GroupOpsByEntity(PendingOps& pendingOps);

        /**
         * @brief 1 Entity 分の全 PendingOp を処理する（最重要オーケストレータ）。
         *
         * Destroy 優先 → 生存チェック → Archetype 決定 → 差分計算 → 確定 →
         * Patch 適用、という一連のフローを順に実行します。
         *
         * @param entity            対象の Entity ID
         * @param ops               この Entity に対する全 PendingOp
         * @param alreadyDestroyed  既に削除済みの Entity セット（多重削除防止）
         * @param destroyedEntities 実際に削除された Entity の蓄積先
         */
        void ProcessEntityOps(
            EntityID entity,
            const std::vector<PendingOp*>& ops,
            std::unordered_set<EntityID>& alreadyDestroyed,
            std::vector<EntityID>& destroyedEntities);

        /// @brief ops に Destroy マーカーが含まれているかを判定する
        bool HasDestroyOp(const std::vector<PendingOp*>& ops) const;

        /**
         * @brief Destroy マーカーが付いた Entity の実際の破棄処理。
         *
         * 全コンポーネントのデストラクタを呼び、Chunk 上で削除マーク、住所録
         * から抹消、destroyedEntities への登録までを実行します。
         */
        void ApplyDestroyOp(
            EntityID entity,
            std::unordered_set<EntityID>& alreadyDestroyed,
            std::vector<EntityID>& destroyedEntities);

        /**
         * @brief この Entity が次に持つべき Archetype のベースを決定する。
         *
         * - ops に Spawn があれば → spawnArchetype を採用、isNewSpawn=true
         * - 無ければ              → srcChunk の Archetype を引き継ぐ
         *
         * @return Archetype を決められなければ false（既存もなく Spawn もない）
         */
        bool DetermineNextArchetype(
            const std::vector<PendingOp*>& ops,
            Chunk* srcChunk,
            Archetype& outArchetype,
            bool& outIsNewSpawn) const;

        /**
         * @brief ops の Add / Remove 操作を走査し、nextArch を最終形へ更新する。
         *
         * @param ops          走査対象の操作群
         * @param nextArch     [in/out] 入出力する Archetype（差分を反映）
         * @param outAddOps    実際に初期データとして使う Add 操作の抽出先
         * @param outRemoveIDs 削除対象 TypeID の抽出先
         */
        void CalculateArchetypeDiff(
            const std::vector<PendingOp*>& ops,
            Archetype& nextArch,
            std::vector<PendingOp*>& outAddOps,
            std::vector<TypeID>& outRemoveIDs) const;

        /**
         * @brief 既存 Entity の Archetype が変わらなかった場合の、コンポーネント
         *        値の上書き処理（Chunk 移動なし）。
         *
         * - factory がある場合      : 一度破棄 → 新規構築（Move-Only 型対応）
         * - data がある場合         : assigner で代入（コピー可能型）
         */
        void UpdateExistingComponents(
            Chunk* srcChunk, size_t srcIndex,
            const std::vector<PendingOp*>& ops);

        /**
         * @brief Patch 操作（部分書き換え）を Entity の最終位置に対して適用。
         *
         * Spawn や Move が完了した後の確定アドレスに対して patcher ラムダを呼びます。
         */
        void ApplyPatchOps(
            EntityID entity,
            const std::vector<PendingOp*>& ops);

        /**
         * @brief 全 Chunk に対して CompactHoles を呼び、削除穴を詰める。
         *
         * 詰める際に Entity の位置が変わるため、内部キャッシュと外部
         * コールバック (m_onEntityMove) の両方を更新します。
         */
        void CompactAllChunks();


        // ----------------------------------------------------------
        // EntityLocation キャッシュ管理
        // ----------------------------------------------------------

        /// @brief Entity の位置情報をキャッシュする内部構造体
        struct EntityLocation
        {
            size_t chunkIndex;  ///< どの Chunk にいるか
            size_t entityIndex; ///< Chunk 内のどのインデックスか

            EntityLocation()
                : chunkIndex(InvalidIndex), entityIndex(InvalidIndex) {}
            EntityLocation(size_t ci, size_t ei)
                : chunkIndex(ci), entityIndex(ei) {}
        };

        /// @brief Entity の位置を更新する（内部ヘルパー）
        void UpdateEntityLocation(EntityID entity, size_t chunkIdx, size_t entityIdx);

        /// @brief Entity の位置情報を削除する（内部ヘルパー）
        void RemoveEntityLocation(EntityID entity);

        /// @brief Chunk* から内部配列のインデックスを取得
        size_t GetChunkIndex(const Chunk* chunk) const;

        /// @brief 1 Chunk あたりの収容人数を Archetype から算出
        size_t CalculateChunkCapacity(const Archetype& archetype) const;


    private:
        // ----------------------------------------------------------
        // データメンバ
        // ----------------------------------------------------------

        /// @brief 全 Chunk のリスト。アーキタイプごとに1つ以上の Chunk が存在
        std::vector<std::unique_ptr<Chunk>> m_chunks;

        /**
         * @brief 1 Chunk のメモリサイズ（固定 16 KB）。
         *
         * L1/L2 キャッシュに収まりやすい標準サイズ。16384 = 2^14 は CPU の
         * メモリ管理単位とも相性が良い。
         */
        static constexpr size_t CHUNK_SIZE_BYTES = 16384;

        /// @brief Entity 移動・生成時の通知コールバック（World 側のキャッシュ更新用）
        EntityMoveCallback m_onEntityMove = nullptr;

        /// @brief EntityID → (ChunkIndex, EntityIndex) の高速検索マップ
        std::vector<EntityLocation> m_entityLocationMap;

        /**
         * @brief 構造変更用の共有 mutex
         *
         * - 読み取り（System 実行）: shared_lock（複数スレッド並列 OK）
         * - 書き込み（ApplyPendingOperations）: unique_lock（排他的）
         */
        mutable std::shared_mutex m_structureMutex;
    };

} // namespace CCL::ECS
