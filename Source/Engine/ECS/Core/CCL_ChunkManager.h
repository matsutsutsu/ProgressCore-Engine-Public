#pragma once
// ===========================================================================
// 概要: 全 Chunk を統括し、Entity の生成・破棄・Archetype 遷移（Chunk 間の引っ越し）を
//       行う ECS 構造変更の中核クラス。
//
// 責務:
//   - Archetype ごとの Chunk の生成と検索
//   - PendingOps の一括適用（Spawn / Destroy / Add / Remove / Patch）
//   - 住所録 m_entityLocationMap（EntityID → Chunk 番号・スロット番号）の維持
//   - 構造変更用 mutex の提供（System 実行中は shared、適用中は unique）
//
// 備考:
//   【構造変更はフレーム境界でしか起きない】
//     System 実行中に Chunk の中身が動くと、走っているループが握るポインタが無効になる。
//     Spawn / Destroy / AddComponent / RemoveComponent はすべて PendingOps へ予約として積まれ、
//     World::ScrutinyAndApply から ApplyPendingOperations が一括で適用する。
//     即時実行版（AddEntity / DestroyEntity / CreateChunk）は初期化とテスト用で、
//     ゲームコードからの経路ではない。
//
//   【住所録 m_entityLocationMap】
//     「EntityID → (Chunk 番号, スロット番号)」を index 添字の配列で持つので O(1) で引ける。
//     代わりにスロットが動く操作（生成・引っ越し・穴詰め）すべてで更新しないと、
//     SearchEntityIn が古いスロットを返して「別の Entity のデータが見える」形で壊れる。
//     更新は UpdateEntityLocation / RemoveEntityLocation の2つに集約してある。
//
//   【破棄は2段階】
//     中身をデストラクタで壊すのと、空いたスロットを詰めるのを分離している。
//     1体消すたびに詰めるとスロットがずれるたびに住所録の更新が要るため、
//     穴詰め（CompactAllChunks）はフレーム末尾に1回だけまとめる。
//
//   【型消去】
//     Chunk は生メモリなので、構築も破棄も TypeData が持つ関数ポインタ経由で行う。
//     factory / mover / constructor / assigner / memcpy のどれを選ぶかが値の壊れ方を
//     決めるため、.cpp の各関数のコメントで優先順位とその理由を明示している。
// ===========================================================================
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
    class ChunkManager
    {
    public:
        ChunkManager()  = default;
        ~ChunkManager() = default;

        // ============================================================
        // 即時実行 API（初期化・テスト用。ゲームコードは PendingOps 経由）
        // ============================================================

        // その Archetype の Chunk が無ければ1つ作る。満員でも既存があれば何もしない
        void CreateChunk(const Archetype &archetype);

        // 全 Chunk を Finalize し、全 Entity とデータを破棄する（World の終了・Clear 時）
        void FinalizeAll();

        // 空きのある Chunk にスロットを確保して住所録に登録する。初期値は書かない（ゼロ埋め）
        void AddEntity(EntityID entity, const Archetype &archetype);

        // スロットに印を付けて住所録から消す。デストラクタは呼ばない（RAII を持つ型はリークする）
        void DestroyEntity(EntityID entity);

        // ============================================================
        // 構造変更の適用
        // ============================================================

        /**
         * @brief PendingOps を一括処理し、構造変更（Spawn/Destroy/Add/Remove/Patch）を全て適用する。
         *
         * 呼ばれるのは World::ScrutinyAndApply の1箇所だけ（＝フレーム境界のみ）。
         * 実行中は unique_lock を握るので、この間 System は1つも動けない。
         *
         * @param pendingOps 適用対象の操作キュー
         * @return 実際に破棄した EntityID。World がこれを RecycleEntityID（世代++）へ回す
         *
         * @note 内部は責務ごとに private 関数へ分割している:
         *       - GroupOpsByEntity        : 操作の Entity 別グルーピング
         *       - ProcessEntityOps        : 1 Entity 分の処理のまとめ役
         *       - HasDestroyOp            : Destroy の有無の判定
         *       - ApplyDestroyOp          : Destroy の確定処理
         *       - DetermineNextArchetype  : 次の Archetype のベース決定（Spawn/既存）
         *       - CalculateArchetypeDiff  : Add/Remove の差分反映
         *       - UpdateExistingComponents: 同一 Archetype 内での値の上書き
         *       - ApplyPatchOps           : Patch の確定処理
         *       - CompactAllChunks        : 全 Chunk の穴詰め
         */
        std::vector<EntityID> ApplyPendingOperations(PendingOps &pendingOps);

        /**
         * @brief 型構成が変わった Entity を別の Chunk へ引っ越させる（Archetype 遷移の実体）。
         *
         * Chunk は「同じ型構成だけ」を集めるので、Health を1つ足した時点で
         * 元の Chunk には居られなくなる。
         *
         * 引っ越し手順:
         *   1. 移動先 Chunk にスロットを確保し、住所録を更新する
         *   2. 共通する型のデータを移動先へ運ぶ（ムーブ優先）
         *   3. 新しく足された型は addOps の初期値で構築する
         *   4. 移動元のデータを破棄し、スロットに削除の印を付ける
         *
         * @return 引っ越せたら true。既に破棄済みだった場合は false
         */
        bool MoveEntityBetweenChunks(
            Chunk &srcChunk, size_t srcIndex,
            Chunk &dstChunk, EntityID entity,
            const std::vector<PendingOp *> &addOps,
            const std::vector<TypeID>      &removeTypeIDs);

        // ============================================================
        // 検索・取得
        // ============================================================

        // Archetype が完全一致する Chunk の添字を返す（満員の Chunk も返る）。無ければ InvalidIndex
        size_t SearchEqualChunk(const Archetype &a) const;

        std::vector<std::unique_ptr<Chunk>>       &GetChunks() { return m_chunks; }
        const std::vector<std::unique_ptr<Chunk>> &GetChunks() const { return m_chunks; }

        /**
         * @brief Entity が今どの Chunk のどのスロットにいるかを、住所録から O(1) で引く。
         *
         * @param entityIndexOut Chunk 内のスロット番号の書き込み先。不要なら nullptr
         * @return Chunk の添字。居なければ InvalidIndex
         *
         * @note 世代チェックをしない。照合は World::IsEntityValid の担当。
         */
        size_t SearchEntityIn(EntityID entity, size_t *entityIndexOut = nullptr) const;

        // Entity の生成・移動時に (EntityID, 移動先 Chunk, スロット番号) で呼ばれるコールバック
        using EntityMoveCallback = std::function<void(EntityID, Chunk *, size_t)>;
        void SetEntityMoveCallback(EntityMoveCallback cb) { m_onEntityMove = cb; }

        // 構造変更用 mutex（System 側は shared_lock で読む）
        std::shared_mutex       &GetStructureMutex() { return m_structureMutex; }
        const std::shared_mutex &GetStructureMutex() const { return m_structureMutex; }

    private:
        // ============================================================
        // Chunk 操作ヘルパー
        // ============================================================

        // Archetype が一致し、かつ空きがある Chunk を返す（無ければ作るので必ず有効）
        Chunk &GetOrCreateAvailableChunk(const Archetype &archetype);

        /**
         * @brief 新規 Entity を指定 Chunk に挿入し、全コンポーネントに初期値を書き込む（Spawn 経路）。
         * @param addOps この Entity への AddComponent 予約（＝初期値の供給源）
         */
        void InsertNewEntity(
            Chunk                          &dstChunk,
            EntityID                        entity,
            const std::vector<PendingOp *> &addOps);

        // ============================================================
        // ApplyPendingOperations の分割関数
        // ============================================================

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
         * @return 値は PendingOp*（実体はコピーしない）。元の pendingOps が生きている間だけ有効
         */
        std::unordered_map<EntityID, std::vector<PendingOp *>>
        GroupOpsByEntity(PendingOps &pendingOps);

        /**
         * @brief 1 Entity 分の全 PendingOp を処理し、この Entity の最終形を確定させる。
         *
         * Destroy 優先 → 生存チェック → Archetype 決定 → 差分計算 → 確定 → Patch 適用、の順。
         *
         * @param entity            対象の Entity ID
         * @param ops               この Entity に対する全 PendingOp
         * @param alreadyDestroyed  既に破棄済みの Entity の集合（多重破棄の防止）
         * @param destroyedEntities 実際に破棄した Entity の蓄積先（World まで返る）
         */
        void ProcessEntityOps(
            EntityID                        entity,
            const std::vector<PendingOp *> &ops,
            std::unordered_set<EntityID>   &alreadyDestroyed,
            std::vector<EntityID>          &destroyedEntities);

        // ops に Destroy が1つでも含まれているか
        bool HasDestroyOp(const std::vector<PendingOp *> &ops) const;

        // 破棄の確定処理。全コンポーネントのデストラクタを呼び、スロットに印を付け、住所録から消す
        void ApplyDestroyOp(
            EntityID                      entity,
            std::unordered_set<EntityID> &alreadyDestroyed,
            std::vector<EntityID>        &destroyedEntities);

        /**
         * @brief Add/Remove を反映する前の、ベースとなる Archetype を決める。
         *
         * - ops に Spawn があれば → spawnArchetype を採用し、outIsNewSpawn = true
         * - 無ければ              → srcChunk の Archetype を引き継ぐ
         *
         * @return Archetype を決められなければ false（既存もなく Spawn もない）
         */
        bool DetermineNextArchetype(
            const std::vector<PendingOp *> &ops,
            Chunk                          *srcChunk,
            Archetype                      &outArchetype,
            bool                           &outIsNewSpawn) const;

        /**
         * @brief ops の Add / Remove を走査し、nextArch を最終形へ更新する。
         *
         * @param ops          走査対象の操作群
         * @param nextArch     [入出力] ベースの Archetype。この関数が書き換えて最終形にする
         * @param outAddOps    初期値として使う Add 操作の抽出先
         * @param outRemoveIDs 削除対象 TypeID の抽出先
         */
        void CalculateArchetypeDiff(
            const std::vector<PendingOp *> &ops,
            Archetype                      &nextArch,
            std::vector<PendingOp *>       &outAddOps,
            std::vector<TypeID>            &outRemoveIDs) const;

        /**
         * @brief Archetype が変わらなかった場合の、コンポーネント値の上書き（Chunk 移動なし）。
         *
         * - factory がある場合: 一度破棄 → 新規構築（ムーブ専用型に対応）
         * - data がある場合   : assigner で代入（コピー可能な型）
         */
        void UpdateExistingComponents(
            Chunk *srcChunk, size_t srcIndex,
            const std::vector<PendingOp *> &ops);

        // Patch（部分書き換え）を、Spawn・引っ越しが済んだ後の最終アドレスに対して適用する
        void ApplyPatchOps(
            EntityID                        entity,
            const std::vector<PendingOp *> &ops);

        // 全 Chunk の削除穴を詰め、動いた Entity の住所録を更新する
        void CompactAllChunks();

        // ============================================================
        // 住所録（EntityLocation）の管理
        // ============================================================

        struct EntityLocation
        {
            size_t chunkIndex;  // どの Chunk にいるか（InvalidIndex なら破棄済み＝墓石）
            size_t entityIndex; // Chunk 内のスロット番号

            EntityLocation()
                : chunkIndex(InvalidIndex), entityIndex(InvalidIndex) {}
            EntityLocation(size_t ci, size_t ei)
                : chunkIndex(ci), entityIndex(ei) {}
        };

        // 住所録に今の位置を書く。スロットが動く操作は必ずここを通す
        void UpdateEntityLocation(EntityID entity, size_t chunkIdx, size_t entityIdx);

        // 住所録から消す（要素は消さず、無効値の墓石で上書きする）
        void RemoveEntityLocation(EntityID entity);

        // Chunk* から m_chunks の添字を引く（線形探索）
        size_t GetChunkIndex(const Chunk *chunk) const;

        // 1 Chunk の定員を Archetype から算出する（CHUNK_SIZE_BYTES ÷ 1体のバイト数）
        size_t CalculateChunkCapacity(const Archetype &archetype) const;

    private:
        std::vector<std::unique_ptr<Chunk>> m_chunks; // 全 Chunk。Archetype ごとに1つ以上

        // 1 Chunk のメモリサイズ（固定 16KB）。
        // Chunk ごと L1/L2 キャッシュに収まりやすい大きさにしている。
        static constexpr size_t CHUNK_SIZE_BYTES = 16384;

        // Entity の生成・移動を外部へ知らせるフック。
        // 位置をキャッシュする側のために用意しているが、現在は誰も登録していない
        EntityMoveCallback m_onEntityMove = nullptr;

        // 住所録。添字は EntityHandle::GetIndex(EntityID)（世代は捨てる）
        std::vector<EntityLocation> m_entityLocationMap;

        // 構造変更用の共有 mutex。
        //   読み取り（System 実行）             : shared_lock（複数スレッドで並列に取れる）
        //   書き込み（ApplyPendingOperations）  : unique_lock（排他）
        mutable std::shared_mutex m_structureMutex;
    };

} // namespace CCL::ECS
