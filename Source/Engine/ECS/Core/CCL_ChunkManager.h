#pragma once
// ===========================================================================
// 概要: 全 Chunk を統括し、Entity の生成・破棄・Archetype 遷移（Chunk 間の引っ越し）を行う ECS の構造変更の中心
//
// 責務:
//   - Archetype ごとの Chunk の生成と検索
//   - PendingOps の一括適用（Spawn / Destroy / Add / Remove / Patch）
//   - 住所録 m_entityLocationMap（EntityID → Chunk 番号・スロット番号）の維持
//   - 構造変更用 mutex の提供（System の実行中は shared、適用中は unique）
//
// 備考: 構造の変更は予約し、フレームの境目（World::ScrutinyAndApply）でまとめて当てる
//       System の実行中に Chunk の中身が動くと、ループが持っているポインタが無効になる
//       スロットが動く操作は、必ず UpdateEntityLocation / RemoveEntityLocation で住所録を直す
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

        // その Archetype の Chunk がなければ1つ作る。満員でも既存があれば何もしない
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

        // PendingOps を一括処理し、構造変更をすべて適用する。実際に破棄した EntityID を返す
        // 呼ぶのは World::ScrutinyAndApply の1か所だけ。実行中は unique_lock を握るので、System は動けない
        // 返した ID は、World が RecycleEntityID（世代を進める）へ回す
        std::vector<EntityID> ApplyPendingOperations(PendingOps &pendingOps);

        // 型構成が変わった Entity を別の Chunk へ引っ越させる（Archetype 遷移の実体）
        // 移動先にスロットを取り、共通の型をムーブで運び、足された型を構築してから、移動元を破棄する
        // 既に破棄済みなら false
        bool MoveEntityBetweenChunks(
            Chunk &srcChunk, size_t srcIndex,
            Chunk &dstChunk, EntityID entity,
            const std::vector<PendingOp *> &addOps,
            const std::vector<TypeID>      &removeTypeIDs);

        // ============================================================
        // 検索・取得
        // ============================================================

        // Archetype が完全一致する Chunk の添字を返す（満員の Chunk も返る）。なければ InvalidIndex
        size_t SearchEqualChunk(const Archetype &a) const;

        std::vector<std::unique_ptr<Chunk>>       &GetChunks() { return m_chunks; }
        const std::vector<std::unique_ptr<Chunk>> &GetChunks() const { return m_chunks; }

        // Entity が今どの Chunk のどのスロットにいるかを、住所録から O(1) で引く。いなければ InvalidIndex
        // 世代は照合しない（照合は World::IsEntityValid が行う）。スロット番号が不要なら nullptr を渡す
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

        // Archetype が一致し、かつ空きがある Chunk を返す（なければ作るので必ず有効）
        Chunk &GetOrCreateAvailableChunk(const Archetype &archetype);

        // 新しい Entity を Chunk に入れ、全コンポーネントに初期値を書く（Spawn の経路）
        // 初期値は、この Entity への AddComponent の予約（addOps）から取る
        void InsertNewEntity(
            Chunk                          &dstChunk,
            EntityID                        entity,
            const std::vector<PendingOp *> &addOps);

        // ============================================================
        // ApplyPendingOperations の分割関数
        // ============================================================

        // PendingOps を Entity ごとに束ねる（[E10 Add, E20 Remove, E10 Add] → E10:[Add, Add], E20:[Remove]）
        // 値は PendingOp* で実体はコピーしない。元の pendingOps が生きている間だけ有効
        std::unordered_map<EntityID, std::vector<PendingOp *>>
        GroupOpsByEntity(PendingOps &pendingOps);

        // 1 Entity 分の PendingOp をすべて処理し、最終形を確定させる
        // Destroy 優先 → 生存の確認 → Archetype の決定 → 差分 → 確定 → Patch の順。破棄した ID は destroyedEntities へ足す
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

        // Add / Remove を反映する前の、元になる Archetype を決める
        // Spawn があればその Archetype（outIsNewSpawn = true）、なければ srcChunk のものを引き継ぐ。どちらもなければ false
        bool DetermineNextArchetype(
            const std::vector<PendingOp *> &ops,
            Chunk                          *srcChunk,
            Archetype                      &outArchetype,
            bool                           &outIsNewSpawn) const;

        // ops の Add / Remove を走査して nextArch を最終形に書き換え、Add の操作と消す TypeID を抜き出す
        void CalculateArchetypeDiff(
            const std::vector<PendingOp *> &ops,
            Archetype                      &nextArch,
            std::vector<PendingOp *>       &outAddOps,
            std::vector<TypeID>            &outRemoveIDs) const;

        // Archetype が変わらないときの、値の上書き（Chunk は移らない）
        // factory があれば一度壊して作り直し（ムーブ専用の型のため）、data があれば assigner で代入する
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

        // 1 Chunk のメモリサイズ（16KB）。Chunk ごと L1/L2 キャッシュに収まりやすい大きさ
        static constexpr size_t CHUNK_SIZE_BYTES = 16384;

        // Entity の生成・移動を外へ知らせるフック。位置をキャッシュする側のためのもので、今は誰も登録していない
        EntityMoveCallback m_onEntityMove = nullptr;

        // 住所録。添字は EntityHandle::GetIndex(EntityID)（世代は捨てる）
        std::vector<EntityLocation> m_entityLocationMap;

        // 構造変更の mutex。System の実行中は shared_lock、ApplyPendingOperations は unique_lock で取る
        mutable std::shared_mutex m_structureMutex;
    };

} // namespace CCL::ECS
