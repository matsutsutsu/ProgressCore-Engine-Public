/**
 * @file   CCL_ChunkManager.cpp
 * @brief  ChunkManagerの実装
 *
 * @details
 *  前バージョンからの修正点:
 *   - GetChunkIndex を Chunk::GetSelfIndex() による O(1) 化
 *   - DestroyEntity に欠落していたデストラクタ呼び出しを追加
 *   - InsertNewEntity の if 分岐を「factory > data > default」の優先順位に整理
 *   - MoveEntityBetweenChunks 内の unordered_map 生成を排除し、
 *     Archetype 内の binary search で代替 (O(log N))
 *
 * @author CCL ECS Library
 */
#include "CCL_ChunkManager.h"

#include <algorithm>
#include <cstring>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <mutex>


namespace CCL::ECS::Core
{
    using namespace CCL::ECS;

    namespace
    {
        /// @brief EntityIDから世代を捨てたpure indexを取得
        inline std::size_t GetPureIndex(EntityID id) noexcept
        {
            return static_cast<std::size_t>(EntityHandle::GetIndex(id));
        }
    }


    // =========================================================================
    //  Chunk生成 / 破棄
    // =========================================================================

    void ChunkManager::CreateChunk(const Archetype& archetype)
    {
        for (auto& c : _chunks) {
            if (c->GetArchetype() == archetype) return;
        }

        auto chunk = std::make_unique<Chunk>();
        chunk->Create(archetype, CalculateChunkCapacity(archetype));
        chunk->SetSelfIndex(_chunks.size());
        _chunks.emplace_back(std::move(chunk));
    }


    void ChunkManager::FinalizeAll()
    {
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        for (auto& c : _chunks) c->Finalize();
        _chunks.clear();
        _entityLocationMap.clear();
    }


    // =========================================================================
    //  Entity 操作 (直接)
    // =========================================================================

    void ChunkManager::AddEntity(EntityID entity, const Archetype& archetype)
    {
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        Chunk& chunk = GetOrCreateAvailableChunk(archetype);
        chunk.InsertEntity(entity);

        const std::size_t chunkIdx = chunk.GetSelfIndex();
        const std::size_t entIdx   = chunk.GetEntityCount() - 1;
        UpdateEntityLocation(entity, chunkIdx, entIdx);
    }


    void ChunkManager::DestroyEntity(EntityID entity)
    {
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        std::size_t entityIndex = 0;
        const std::size_t chunkIndex = SearchEntityIn(entity, &entityIndex);
        if (chunkIndex == InvalidIndex) return;

        Chunk* chunk = _chunks[chunkIndex].get();

        // 既に削除予約済みなら何もしない(二重デストラクタ防止)
        if (chunk->IsEntityDestroyed(entityIndex)) return;

        // 修正: デストラクタを必ず呼んでから MarkDestroyEntity
        for (const TypeData& td : chunk->GetArchetype()) {
            void* ptr = chunk->GetComponentPtrByType(td.id, entityIndex);
            if (ptr) td.destructor.Destruct(ptr);
        }
        chunk->MarkDestroyEntity(entityIndex);
        RemoveEntityLocation(entity);
    }


    // =========================================================================
    //  検索
    // =========================================================================

    std::size_t ChunkManager::SearchEntityIn(EntityID entity, std::size_t* outEntityIndex) const
    {
        const std::size_t index = GetPureIndex(entity);
        if (index >= _entityLocationMap.size()) return InvalidIndex;

        const auto& loc = _entityLocationMap[index];
        if (loc.chunkIndex == InvalidIndex) return InvalidIndex;

        if (outEntityIndex) *outEntityIndex = loc.entityIndex;
        return loc.chunkIndex;
    }


    std::size_t ChunkManager::SearchEqualChunk(const Archetype& a) const
    {
        for (std::size_t i = 0; i < _chunks.size(); ++i) {
            if (_chunks[i]->GetArchetype() == a) return i;
        }
        return InvalidIndex;
    }


    // =========================================================================
    //  内部ヘルパ
    // =========================================================================

    Chunk& ChunkManager::GetOrCreateAvailableChunk(const Archetype& archetype)
    {
        auto it = std::find_if(_chunks.begin(), _chunks.end(),
            [&](const std::unique_ptr<Chunk>& c) {
                return c->GetArchetype() == archetype
                    && c->GetEntityCount() < c->GetCapacity();
            });

        if (it != _chunks.end()) return **it;

        auto chunk = std::make_unique<Chunk>();
        chunk->Create(archetype, CalculateChunkCapacity(archetype));
        chunk->SetSelfIndex(_chunks.size());
        _chunks.emplace_back(std::move(chunk));
        return *_chunks.back();
    }


    void ChunkManager::InsertNewEntity(
        Chunk& dstChunk, EntityID entityID,
        const std::vector<PendingOp*>& addOps)
    {
        const std::size_t newIndex = dstChunk.AddEntity(entityID);
        if (newIndex == InvalidIndex) return;

        const std::size_t chunkIdx = dstChunk.GetSelfIndex();
        UpdateEntityLocation(entityID, chunkIdx, newIndex);

        // 各コンポーネントを初期化
        for (const TypeData& td : dstChunk.GetArchetype())
        {
            void* dstPtr = dstChunk.GetComponentPtrByType(td.id, newIndex);
            if (!dstPtr) continue;

            // 該当addOpを探す(後勝ち: 同じTypeIDが複数あったら最後を採用)
            PendingOp* matchedOp = nullptr;
            for (auto it = addOps.rbegin(); it != addOps.rend(); ++it) {
                if ((*it)->type == td.id) { matchedOp = *it; break; }
            }

            // 修正: 優先順位を明確化 (factory → data → default)
            if (matchedOp) {
                if (matchedOp->factory) {
                    matchedOp->factory(dstPtr);
                }
                else if (matchedOp->data) {
                    if (td.constructor) {
                        td.constructor(dstPtr, matchedOp->data);
                    } else {
                        std::memcpy(dstPtr, matchedOp->data, td.typeSize);
                    }
                }
                else if (td.defaultConstructor) {
                    td.defaultConstructor(dstPtr);
                }
                else {
                    std::memset(dstPtr, 0, td.typeSize);
                }
            }
            else {
                // PendingOpになかった: デフォルト構築 or 0クリア
                if (td.defaultConstructor) {
                    td.defaultConstructor(dstPtr);
                } else {
                    std::memset(dstPtr, 0, td.typeSize);
                }
            }
        }

        if (_onEntityMove) {
            _onEntityMove(entityID, &dstChunk, newIndex);
        }
    }


    bool ChunkManager::MoveEntityBetweenChunks(
        Chunk& srcChunk, std::size_t srcIndex,
        Chunk& dstChunk, EntityID entity,
        const std::vector<PendingOp*>& addOps,
        const std::vector<TypeID>& /*removeTypeIDs*/)
    {
        if (srcChunk.IsEntityDestroyed(srcIndex)) return false;

        const std::size_t newIdx = dstChunk.AddEntity(entity);
        if (newIdx == InvalidIndex) return false;

        UpdateEntityLocation(entity, dstChunk.GetSelfIndex(), newIdx);

        const Archetype& srcArch = srcChunk.GetArchetype();
        const Archetype& dstArch = dstChunk.GetArchetype();

        // 各dstコンポーネントを埋める
        // 修正: unordered_map を使わず、Archetype の binary search で代替
        for (std::size_t di = 0; di < dstArch.size(); ++di)
        {
            const TypeData& tdDst = dstArch[di];
            void* dstPtr = dstChunk.GetComponentPtrByType(tdDst.id, newIdx);
            if (!dstPtr) continue;

            // パターンA: srcから引き継ぐ
            const std::size_t srcCompIdx = srcArch.GetComponentIndex(tdDst.id);
            if (srcCompIdx != InvalidIndex) {
                void* srcPtr = srcChunk.GetComponentPtrByType(tdDst.id, srcIndex);
                if (tdDst.mover) {
                    tdDst.mover(dstPtr, srcPtr);
                } else if (tdDst.constructor) {
                    tdDst.constructor(dstPtr, srcPtr);
                } else {
                    std::memcpy(dstPtr, srcPtr, tdDst.typeSize);
                }
                continue;
            }

            // パターンB: addOps から見つける
            PendingOp* matchedOp = nullptr;
            for (auto it = addOps.rbegin(); it != addOps.rend(); ++it) {
                if ((*it)->type == tdDst.id) { matchedOp = *it; break; }
            }

            if (matchedOp) {
                if (matchedOp->factory) {
                    matchedOp->factory(dstPtr);
                }
                else if (matchedOp->data) {
                    if (tdDst.constructor) {
                        tdDst.constructor(dstPtr, matchedOp->data);
                    } else {
                        std::memcpy(dstPtr, matchedOp->data, tdDst.typeSize);
                    }
                }
                else if (tdDst.defaultConstructor) {
                    tdDst.defaultConstructor(dstPtr);
                }
                else {
                    std::memset(dstPtr, 0, tdDst.typeSize);
                }
            }
            else {
                if (tdDst.defaultConstructor) {
                    tdDst.defaultConstructor(dstPtr);
                } else {
                    std::memset(dstPtr, 0, tdDst.typeSize);
                }
            }
        }

        // src側を破棄
        for (std::size_t si = 0; si < srcArch.size(); ++si) {
            const TypeData& tdSrc = srcArch[si];
            void* srcPtr = srcChunk.GetComponentPtrByType(tdSrc.id, srcIndex);
            if (srcPtr) tdSrc.destructor.Destruct(srcPtr);
        }

        if (_onEntityMove) _onEntityMove(entity, &dstChunk, newIdx);
        srcChunk.MarkDestroyEntity(srcIndex);
        return true;
    }


    // =========================================================================
    //  位置キャッシュ
    // =========================================================================

    void ChunkManager::UpdateEntityLocation(EntityID entity, std::size_t chunkIdx, std::size_t entityIdx)
    {
        const std::size_t index = GetPureIndex(entity);
        if (index >= _entityLocationMap.size()) {
            _entityLocationMap.resize(index + 1024); // ある程度まとめて拡張
        }
        _entityLocationMap[index] = EntityLocation(chunkIdx, entityIdx);
    }


    void ChunkManager::RemoveEntityLocation(EntityID entity)
    {
        const std::size_t index = GetPureIndex(entity);
        if (index < _entityLocationMap.size()) {
            _entityLocationMap[index] = EntityLocation();
        }
    }


    std::size_t ChunkManager::GetChunkIndex(const Chunk* chunk) const
    {
        // 修正: Chunkが自分のインデックスを知っているので O(1)
        if (!chunk) return InvalidIndex;
        return chunk->GetSelfIndex();
    }


    // =========================================================================
    //  容量計算
    // =========================================================================

    std::size_t ChunkManager::CalculateChunkCapacity(const Archetype& archetype) const
    {
        std::size_t stride = 0;
        for (const auto& t : archetype) stride += t.typeSize;

        if (stride == 0) return 1024; // タグだけのEntity用

        const std::size_t capacity = CHUNK_SIZE_BYTES / stride;
        return (capacity > 0) ? capacity : 1;
    }


    // =========================================================================
    //  ApplyPendingOperations: 構造変更を一括適用
    // =========================================================================

    std::vector<EntityID> ChunkManager::ApplyPendingOperations(PendingOps& pendingOps)
    {
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        std::vector<EntityID> destroyedEntities;
        if (pendingOps.Ops().empty()) return destroyedEntities;

        // ── Entityごとに操作をまとめる ────────────────────────────────
        std::unordered_map<EntityID, std::vector<PendingOp*>> opsByEntity;
        opsByEntity.reserve(pendingOps.Ops().size());

        for (auto& op : pendingOps.Ops()) {
            opsByEntity[op.entity].push_back(&op);
        }

        std::unordered_set<EntityID> alreadyDestroyed;

        for (auto& [entityID, ops] : opsByEntity)
        {
            // ── Destroy優先処理 ───────────────────────────────────
            bool hasDestroy = false;
            for (auto* op : ops) {
                if (op->kind == PendingOpKind::Destroy) { hasDestroy = true; break; }
            }

            if (hasDestroy) {
                if (alreadyDestroyed.count(entityID)) continue;

                std::size_t entityIndex = 0;
                const std::size_t chunkIdx = SearchEntityIn(entityID, &entityIndex);
                if (chunkIdx != InvalidIndex) {
                    Chunk* cptr = _chunks[chunkIdx].get();
                    if (!cptr->IsEntityDestroyed(entityIndex)) {
                        for (const TypeData& td : cptr->GetArchetype()) {
                            void* ptr = cptr->GetComponentPtrByType(td.id, entityIndex);
                            if (ptr) td.destructor.Destruct(ptr);
                        }
                        cptr->MarkDestroyEntity(entityIndex);
                        RemoveEntityLocation(entityID);

                        alreadyDestroyed.insert(entityID);
                        destroyedEntities.push_back(entityID);
                    }
                }
                continue;
            }

            // ── 既存状態の確認 ─────────────────────────────────────
            std::size_t srcIndex = 0;
            const std::size_t chunkIdx = SearchEntityIn(entityID, &srcIndex);
            Chunk* srcChunk = (chunkIdx != InvalidIndex) ? _chunks[chunkIdx].get() : nullptr;

            if (alreadyDestroyed.count(entityID)) continue;
            if (srcChunk && srcChunk->IsEntityDestroyed(srcIndex)) continue;

            // ── 次のArchetypeを計算 ──────────────────────────────
            Archetype nextArch;
            bool      isNewSpawn = false;
            for (auto* op : ops) {
                if (op->kind == PendingOpKind::Spawn) {
                    nextArch    = op->spawnArchetype;
                    isNewSpawn  = true;
                    break;
                }
            }

            if (!isNewSpawn) {
                if (!srcChunk) continue;
                nextArch = srcChunk->GetArchetype();
            }

            // ── 差分の集計 ──────────────────────────────────────
            std::vector<PendingOp*> addOps;
            std::vector<TypeID>     removeIDs;

            for (auto* op : ops) {
                if (op->kind == PendingOpKind::AddComponent) {
                    addOps.push_back(op);

                    if (nextArch.GetComponentIndex(op->type) == InvalidIndex) {
                        TypeData newType;
                        newType.id          = op->type;
                        newType.typeSize    = op->typeSize;
                        newType.destructor  = op->destructor;
                        newType.constructor = op->constructor;
                        newType.assigner    = op->assigner;
                        newType.mover       = op->mover;
                        newType.name        = op->name;
                        nextArch.Add(newType);
                    }
                }
                else if (op->kind == PendingOpKind::RemoveComponent) {
                    removeIDs.push_back(op->type);
                    nextArch.Sub(op->type);
                }
            }

            // ── 確定処理 ─────────────────────────────────────────
            if (isNewSpawn) {
                Chunk& dstChunk = GetOrCreateAvailableChunk(nextArch);
                InsertNewEntity(dstChunk, entityID, addOps);
            }
            else if (!(nextArch == srcChunk->GetArchetype())) {
                Chunk& dstChunk = GetOrCreateAvailableChunk(nextArch);
                if (!srcChunk->IsEntityDestroyed(srcIndex)) {
                    MoveEntityBetweenChunks(*srcChunk, srcIndex, dstChunk, entityID, addOps, removeIDs);
                }
            }
            else {
                // Archetype変化なし: 既存スロットに上書き
                for (auto* op : ops) {
                    if (op->kind != PendingOpKind::AddComponent) continue;

                    void* dstPtr = srcChunk->GetComponentPtrByType(op->type, srcIndex);
                    if (!dstPtr || (!op->factory && !op->data)) continue;

                    const std::size_t compIdx = srcChunk->GetArchetype().GetComponentIndex(op->type);
                    if (compIdx == InvalidIndex) continue;
                    const TypeData& td = srcChunk->GetArchetype()[compIdx];

                    if (op->factory) {
                        td.destructor.Destruct(dstPtr);
                        op->factory(dstPtr);
                    }
                    else if (op->data) {
                        if (td.assigner) td.assigner(dstPtr, op->data);
                        else             std::memcpy(dstPtr, op->data, td.typeSize);
                    }
                }
            }

            // ── Patchの適用 ──────────────────────────────────────
            std::size_t finalIndex    = 0;
            const std::size_t finalChunkIdx = SearchEntityIn(entityID, &finalIndex);
            if (finalChunkIdx != InvalidIndex) {
                Chunk* finalChunk = _chunks[finalChunkIdx].get();
                for (auto* op : ops) {
                    if (op->kind != PendingOpKind::PatchComponent) continue;
                    if (!op->patcher) continue;
                    if (void* ptr = finalChunk->GetComponentPtrByType(op->type, finalIndex)) {
                        op->patcher(ptr);
                    }
                }
            }
        }

        // ── 穴を詰める ───────────────────────────────────────────
        for (auto& c : _chunks) {
            c->CompactHoles([this](EntityID e, Chunk* chunk, std::size_t newIdx) {
                UpdateEntityLocation(e, chunk->GetSelfIndex(), newIdx);
                if (_onEntityMove) _onEntityMove(e, chunk, newIdx);
            });
        }

        return destroyedEntities;
    }

} // namespace CCL::ECS::Core
