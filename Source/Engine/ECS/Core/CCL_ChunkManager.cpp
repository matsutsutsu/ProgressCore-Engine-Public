#include "CCL_ChunkManager.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <cassert>

namespace CCL::ECS
{
    // ============================================================
    // 内部ユーティリティ
    // ============================================================

    // EntityID から世代を捨て、住所録の添字を取り出す。世代の照合は World::IsEntityValid がする
    // ビットの並びを2か所に書かないよう、EntityHandle::GetIndex を通す
    inline size_t GetPureIndex(EntityID id)
    {
        return static_cast<size_t>(EntityHandle::GetIndex(id));
    }



    // ============================================================
    // 即時実行 API
    // ============================================================

    void ChunkManager::CreateChunk(const Archetype &archetype)
    {
        // 「空きがあるか」は見ない。空きまで保証したいときは GetOrCreateAvailableChunk を使う

        for (auto &c : m_chunks)
        {
            if (c->GetArchetype() == archetype)
                return;
        }
        m_chunks.emplace_back(std::make_unique<Chunk>());

        size_t capacity = CalculateChunkCapacity(archetype);
        m_chunks.back()->Create(archetype, capacity);
    }



    void ChunkManager::FinalizeAll()
    {
        // 構造そのものを壊す操作なので、共有ロックではなく unique_lock を取る

        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        for (auto &c : m_chunks)
            c->Finalize();
        m_chunks.clear();

        m_entityLocationMap.clear();
    }



    void ChunkManager::AddEntity(EntityID entity, const Archetype &archetype)
    {
        // 予約を通さずにすぐ作る。ゲームコードの Spawn は、予約から InsertNewEntity を通る
        // 初期値は書かないので、スロットはゼロクリアのまま

        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        auto it = std::find_if(m_chunks.begin(), m_chunks.end(),
                               [&](const std::unique_ptr<Chunk> &c) {
                                   return c->GetArchetype() == archetype &&
                                          c->GetEntityCount() < c->GetCapacity();
                               });

        if (it == m_chunks.end())
        {
            m_chunks.emplace_back(std::make_unique<Chunk>());
            size_t capacity = CalculateChunkCapacity(archetype);
            m_chunks.back()->Create(archetype, capacity);
            it = std::prev(m_chunks.end());
        }

        (*it)->InsertEntity(entity);

        size_t chunkIdx  = std::distance(m_chunks.begin(), it);
        size_t entityIdx = (*it)->GetEntityCount() - 1;
        UpdateEntityLocation(entity, chunkIdx, entityIdx);
    }



    void ChunkManager::DestroyEntity(EntityID entity)
    {
        // デストラクタは呼ばない（呼ぶのは予約から来る ApplyDestroyOp）
        // RAII を持つコンポーネントがリークするので、ゲームコードは World::RequestDestroyEntity を使う

        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        size_t entityIndex;
        size_t chunkIndex = SearchEntityIn(entity, &entityIndex);
        if (chunkIndex != InvalidIndex)
        {
            m_chunks[chunkIndex]->MarkDestroyEntity(entityIndex);
            RemoveEntityLocation(entity);
        }
    }



    size_t ChunkManager::SearchEntityIn(EntityID entity, size_t *outEntityIndex) const
    {
        // 世代は照合しない。古い ID を渡すと、index を使い回した別の Entity の住所が返る
        // 照合は World::IsEntityValid がする。World::GetComponent はそこを通ってからここへ来る

        size_t index = GetPureIndex(entity);

        if (index >= m_entityLocationMap.size()) return InvalidIndex;

        const auto &loc = m_entityLocationMap[index];

        // 墓石（破棄済みでスロットがない）
        if (loc.chunkIndex == InvalidIndex) return InvalidIndex;

        if (outEntityIndex) *outEntityIndex = loc.entityIndex;
        return loc.chunkIndex;
    }



    Chunk &ChunkManager::GetOrCreateAvailableChunk(const Archetype &archetype)
    {
        // なければ作るので、戻り値は必ず有効
        // 定員に達した Chunk は検索で弾くので、同じ型構成の2個目以降の Chunk はここで生まれる

        auto it = std::find_if(m_chunks.begin(), m_chunks.end(),
                               [&](const std::unique_ptr<Chunk> &c) {
                                   return c->GetArchetype() == archetype &&
                                          c->GetEntityCount() < c->GetCapacity();
                               });

        if (it == m_chunks.end())
        {
            m_chunks.emplace_back(std::make_unique<Chunk>());
            size_t capacity = CalculateChunkCapacity(archetype);
            m_chunks.back()->Create(archetype, capacity);
            return *m_chunks.back();
        }
        return **it;
    }



    void ChunkManager::InsertNewEntity(
        Chunk                          &dstChunk,
        EntityID                        entityID,
        const std::vector<PendingOp *> &addOps)
    {
        // 値はムーブを先に試す（factory > mover > constructor > memcpy の順）
        // コピーで作ると、PendingOps::Clear が元を破棄したときに RAII の資源が解放され、複製先が壊れる

        size_t newIndex = dstChunk.AddEntity(entityID);

        size_t chunkIdx = GetChunkIndex(&dstChunk);
        UpdateEntityLocation(entityID, chunkIdx, newIndex);

        for (const TypeData &td : dstChunk.GetTypeDatas())
        {
            void *dstPtr = dstChunk.GetComponentPtrByType(td.id, newIndex);
            if (!dstPtr) continue;

            bool filled = false;

            // 同じ型へ何度も Set したときは、後から積んだ値を使う
            for (int i = (int)addOps.size() - 1; i >= 0; --i)
            {
                auto *op = addOps[i];
                if (op->type == td.id)
                {
                    // factory は Emplace 経路。コピー禁止型もここで構築される
                    if (op->factory)
                    {
                        op->factory(dstPtr);
                    }
                    // 一時メモリからの移送は必ずムーブを先に試す
                    if (op->data && td.mover)
                    {
                        td.mover(dstPtr, op->data); // 元は抜け殻になるので、Clear で破棄しても安全
                    }
                    else if (op->data && td.constructor)
                    {
                        td.constructor(dstPtr, op->data); // ムーブを持たない型のフォールバック
                    }
                    else if (op->data)
                    {
                        std::memcpy(dstPtr, op->data, td.typeSize);
                    }
                    filled = true;
                    break;
                }
            }

            // 初期値の指定がない型はデフォルト構築、それもなければゼロクリア
            if (!filled)
            {
                if (td.defaultConstructor)
                {
                    td.defaultConstructor(dstPtr);
                }
                else
                {
                    std::memset(dstPtr, 0, td.typeSize);
                }
            }
        }

        // 外部のフックへ実体化を知らせる（未登録なら何もしない）
        if (m_onEntityMove)
        {
            m_onEntityMove(entityID, &dstChunk, newIndex);
        }
    }



    size_t ChunkManager::SearchEqualChunk(const Archetype &a) const
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (m_chunks[i]->GetArchetype() == a)
                return i;
        }
        return InvalidIndex;
    }



    void ChunkManager::UpdateEntityLocation(EntityID entity, size_t chunkIdx, size_t entityIdx)
    {
        // スロットが動く操作（生成・引っ越し・穴詰め）は必ずここを通す
        // 漏れると SearchEntityIn が古いスロットを返し、別の Entity のデータを読む

        size_t index = GetPureIndex(entity);

        if (index >= m_entityLocationMap.size())
        {
            m_entityLocationMap.resize(index + 1);
        }

        m_entityLocationMap[index] = EntityLocation(chunkIdx, entityIdx);
    }



    void ChunkManager::RemoveEntityLocation(EntityID entity)
    {
        // erase せず無効値で上書きする（墓石）
        // 住所録は index を添字に引くので、途中を消すと後ろの住所がすべてずれる

        size_t index = GetPureIndex(entity);

        if (index < m_entityLocationMap.size())
        {
            m_entityLocationMap[index] = EntityLocation(InvalidIndex, InvalidIndex);
        }
    }



    size_t ChunkManager::GetChunkIndex(const Chunk *chunk) const
    {
        // 線形探索なので、Chunk が増えるほど遅い。引っ越しの多いフレームでは 移動数 × Chunk 数 になる
        // 遅くなったら、Chunk に自分の添字を持たせて引く

        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (m_chunks[i].get() == chunk)
            {
                return i;
            }
        }
        return InvalidIndex;
    }



    size_t ChunkManager::CalculateChunkCapacity(const Archetype &archetype) const
    {
        // Chunk を 16KB に固定し、Chunk ごと CPU のキャッシュに載せる。定員は型構成で変わる
        // タグ型だけ（stride が 0）なら 1024、1体が 16KB を超えるなら 1 を返す

        size_t stride = 0;
        for (const auto &t : archetype)
        {
            stride += t.typeSize;
        }

        if (stride == 0) return 1024;

        size_t capacity = CHUNK_SIZE_BYTES / stride;
        return capacity > 0 ? capacity : 1;
    }



    // ============================================================
    // Archetype 遷移
    // ============================================================

    bool ChunkManager::MoveEntityBetweenChunks(
        Chunk                          &srcChunk,
        size_t                          srcIndex,
        Chunk                          &dstChunk,
        EntityID                        entity,
        const std::vector<PendingOp *> &addOps,
        const std::vector<TypeID>      &removeTypeIDs)
    {
        // 引っ越し先の列は、元にもある型ならムーブで運び、足された型なら addOps の初期値で作り、
        // どちらでもなければゼロクリアする（下のパターン A / B / C）
        // 元のスロットは印を付けるだけで、詰めるのはフレームの最後の CompactAllChunks

        if (srcChunk.IsEntityDestroyed(srcIndex))
        {
            return false;
        }

        dstChunk.InsertEntity(entity);
        size_t dstIndex = dstChunk.GetEntityCount() - 1;

        // データを運ぶ前に住所を更新する（以降は新しいスロットを前提に読む）
        size_t dstChunkIdx = GetChunkIndex(&dstChunk);
        UpdateEntityLocation(entity, dstChunkIdx, dstIndex);

        const Archetype &srcArch = srcChunk.GetArchetype();
        const Archetype &dstArch = dstChunk.GetArchetype();

        auto BuildIndexMap = [](const Archetype &a) {
            std::unordered_map<TypeID, size_t> m;
            for (size_t i = 0; i < a.size(); ++i)
                m[a[i].id] = i;
            return m;
        };
        auto srcMap = BuildIndexMap(srcArch);
        auto dstMap = BuildIndexMap(dstArch);

        for (size_t di = 0; di < dstArch.size(); ++di)
        {
            const TypeData &tdDst  = dstArch[di];
            void           *dstPtr = dstChunk.GetComponentPtrByType(tdDst.id, dstIndex);

            // パターンA: 元の Chunk から運ぶ
            auto itSrc = srcMap.find(tdDst.id);
            if (itSrc != srcMap.end())
            {
                void *srcPtr = srcChunk.GetComponentPtrByType(tdDst.id, srcIndex);

                if (tdDst.mover)
                {
                    tdDst.mover(dstPtr, srcPtr);
                }
                else if (tdDst.constructor)
                {
                    tdDst.constructor(dstPtr, srcPtr);
                }
                else
                {
                    std::memcpy(dstPtr, srcPtr, tdDst.typeSize);
                }
            }
            // パターンB: 新しく足された型
            else
            {
                bool filled = false;
                for (PendingOp *op : addOps)
                {
                    if (op->type == tdDst.id)
                    {
                        if (op->factory)
                        {
                            op->factory(dstPtr);
                            filled = true;
                            break;
                        }
                        else if (op->data)
                        {
                            if (tdDst.constructor)
                                tdDst.constructor(dstPtr, op->data);
                            else
                                std::memcpy(dstPtr, op->data, tdDst.typeSize);
                            filled = true;
                            break;
                        }
                    }
                }
                // パターンC: 指定がなければゼロクリア
                if (!filled)
                {
                    std::memset(dstPtr, 0, tdDst.typeSize);
                }
            }
        }

        // 移動元はムーブ後の抜け殻になっているので破棄する
        for (size_t si = 0; si < srcArch.size(); ++si)
        {
            const TypeData &tdSrc  = srcArch[si];
            void           *srcPtr = srcChunk.GetComponentPtrByType(tdSrc.id, srcIndex);
            tdSrc.destructor.Destruct(srcPtr);
        }

        if (m_onEntityMove)
        {
            m_onEntityMove(entity, &dstChunk, dstIndex);
        }

        srcChunk.MarkDestroyEntity(srcIndex);

        return true;
    }



    // ============================================================
    // 予約の適用
    // ============================================================

    std::vector<EntityID> ChunkManager::ApplyPendingOperations(PendingOps &pendingOps)
    {
        // 穴詰めは最後の CompactAllChunks でまとめてする
        // 1体ごとに詰めると、スロットがずれるたびに住所録を直すことになる

        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        std::vector<EntityID> destroyedEntities;
        if (pendingOps.Ops().empty()) return destroyedEntities;

        // 予約は積んだ順に並んでいるので、Entity ごとに束ね直す
        // 束ねると最終的な型構成を一度で求められ、コンポーネントを足すたびに引っ越さずに済む
        auto opsByEntity = GroupOpsByEntity(pendingOps);

        // 同じ Entity を2回破棄しないための記録
        std::unordered_set<EntityID> alreadyDestroyed;

        for (auto &[entityID, ops] : opsByEntity)
        {
            ProcessEntityOps(entityID, ops, alreadyDestroyed, destroyedEntities);
        }

        CompactAllChunks();

        return destroyedEntities;
    }



    std::unordered_map<EntityID, std::vector<PendingOp *>>
    ChunkManager::GroupOpsByEntity(PendingOps &pendingOps)
    {
        std::unordered_map<EntityID, std::vector<PendingOp *>> opsByEntity;
        for (auto &op : pendingOps.Ops())
        {
            opsByEntity[op.entity].push_back(&op);
        }
        return opsByEntity;
    }



    void ChunkManager::ProcessEntityOps(
        EntityID                        entityID,
        const std::vector<PendingOp *> &ops,
        std::unordered_set<EntityID>   &alreadyDestroyed,
        std::vector<EntityID>          &destroyedEntities)
    {
        // 型構成が変わったかどうかで分かれる（ステップ5）
        // Spawn なら新しく作り、変わったら引っ越し、同じなら値を上書きするだけ

        // ステップ1: 破棄が混ざっていれば最優先。消えるものに他の操作をしても無意味
        if (HasDestroyOp(ops))
        {
            ApplyDestroyOp(entityID, alreadyDestroyed, destroyedEntities);
            return;
        }

        // ステップ2: 今どこにいるか
        size_t srcIndex = 0;
        size_t chunkIdx = SearchEntityIn(entityID, &srcIndex);
        Chunk *srcChunk = (chunkIdx != InvalidIndex) ? m_chunks[chunkIdx].get() : nullptr;

        // 次の2つは今は通らない。1体につき1回しか来ず、MarkDestroyEntity を呼ぶ側は先に住所録を直すため
        // 破棄の判定を1か所に頼らないために残している
        if (alreadyDestroyed.find(entityID) != alreadyDestroyed.end())
        {
            return;
        }
        if (srcChunk && srcChunk->IsEntityDestroyed(srcIndex))
        {
            return;
        }

        // ステップ3: 差分を適用する前のベースとなる型構成を決める
        Archetype nextArch;
        bool      isNewSpawn = false;
        if (!DetermineNextArchetype(ops, srcChunk, nextArch, isNewSpawn))
        {
            return; // 存在しないし Spawn でもない
        }

        // ステップ4: Add/Remove を反映して最終形にする
        std::vector<PendingOp *> addOps;
        std::vector<TypeID>      removeIDs;
        CalculateArchetypeDiff(ops, nextArch, addOps, removeIDs);

        // ステップ5: 3分岐で確定させる
        if (isNewSpawn)
        {
            Chunk &dstChunk = GetOrCreateAvailableChunk(nextArch);
            InsertNewEntity(dstChunk, entityID, addOps);
        }
        else if (!(nextArch == srcChunk->GetArchetype()))
        {
            Chunk &dstChunk = GetOrCreateAvailableChunk(nextArch);

            if (!srcChunk->IsEntityDestroyed(srcIndex))
            {
                MoveEntityBetweenChunks(*srcChunk, srcIndex, dstChunk, entityID, addOps, removeIDs);
            }
        }
        else
        {
            UpdateExistingComponents(srcChunk, srcIndex, ops);
        }

        // ステップ6: Patch は引っ越しでアドレスが変わった後に適用する（ApplyPatchOps 参照）
        ApplyPatchOps(entityID, ops);
    }



    bool ChunkManager::HasDestroyOp(const std::vector<PendingOp *> &ops) const
    {
        // 他の判定より先に呼ばれ、true なら残りの予約はすべて捨てられる

        for (auto *op : ops)
        {
            if (op->kind == PendingOpKind::Destroy)
            {
                return true;
            }
        }
        return false;
    }



    void ChunkManager::ApplyDestroyOp(
        EntityID                      entityID,
        std::unordered_set<EntityID> &alreadyDestroyed,
        std::vector<EntityID>        &destroyedEntities)
    {
        // 中身はここで破棄し、スロットは印を付けるだけにする（詰めるのは CompactAllChunks）
        // Chunk は生のメモリなので、TypeData の destructor で破棄する。飛ばすと RAII を持つ型がリークする

        if (alreadyDestroyed.find(entityID) != alreadyDestroyed.end())
        {
            return;
        }

        size_t entityIndex = 0;
        size_t chunkIdx    = SearchEntityIn(entityID, &entityIndex);
        if (chunkIdx == InvalidIndex) return; // 既にいない

        Chunk *cptr = m_chunks[chunkIdx].get();

        // 印が付いていれば、すぐ消す DestroyEntity か引っ越しで処理済み
        // ここで destructor を呼ぶと二重に破棄するので、記録だけして抜ける
        if (cptr->IsEntityDestroyed(entityIndex))
        {
            alreadyDestroyed.insert(entityID);
            return;
        }

        for (const TypeData &td : cptr->GetArchetype())
        {
            void *ptr = cptr->GetComponentPtrByType(td.id, entityIndex);
            td.destructor.Destruct(ptr);
        }

        cptr->MarkDestroyEntity(entityIndex);
        RemoveEntityLocation(entityID);

        alreadyDestroyed.insert(entityID);
        destroyedEntities.push_back(entityID);
    }



    bool ChunkManager::DetermineNextArchetype(
        const std::vector<PendingOp *> &ops,
        Chunk                          *srcChunk,
        Archetype                      &outArchetype,
        bool                           &outIsNewSpawn) const
    {
        // spawnArchetype を値で持つのは、参照だと FrameAllocator の寿命が切れたときに宙に浮くため

        outIsNewSpawn = false;

        for (auto *op : ops)
        {
            if (op->kind == PendingOpKind::Spawn)
            {
                outArchetype  = op->spawnArchetype;
                outIsNewSpawn = true;
                return true;
            }
        }

        if (!srcChunk)
        {
            return false;
        }
        outArchetype = srcChunk->GetArchetype();
        return true;
    }



    void ChunkManager::CalculateArchetypeDiff(
        const std::vector<PendingOp *> &ops,
        Archetype                      &nextArch,
        std::vector<PendingOp *>       &outAddOps,
        std::vector<TypeID>            &outRemoveIDs) const
    {
        // AddComponent は、すでにその型を持っていても outAddOps へ積む。型構成が変わらないときの上書きにも使う

        // 新しい型には destructor などの関数ポインタも詰める
        // 欠けると memcpy で運ばれ、RAII を持つ型が壊れる

        for (auto *op : ops)
        {
            if (op->kind == PendingOpKind::AddComponent)
            {
                outAddOps.push_back(op);

                bool has = false;
                for (auto &td : nextArch)
                {
                    if (td.id == op->type)
                    {
                        has = true;
                        break;
                    }
                }
                if (!has)
                {
                    // T の関数ポインタは AddComponent<T> を通るときしか取れないので、PendingOp が運んでくる
                    TypeData newType;
                    newType.id          = op->type;
                    newType.typeSize    = op->typeSize;
                    newType.destructor  = op->destructor;
                    newType.constructor = op->constructor;
                    newType.assigner    = op->assigner;
                    newType.mover       = op->mover;
                    newType.name        = op->name;

                    // Add / Sub は TypeID の昇順を保つ位置へ入れる
                    nextArch.Add(newType);
                }
            }
            else if (op->kind == PendingOpKind::RemoveComponent)
            {
                outRemoveIDs.push_back(op->type);
                nextArch.Sub(op->type);
            }
        }
    }



    void ChunkManager::UpdateExistingComponents(
        Chunk *srcChunk, size_t srcIndex,
        const std::vector<PendingOp *> &ops)
    {
        // 最終の型構成が今と同じとき（例: Position を持つ Entity へもう一度 Set した）に呼ばれる
        // 構築済みのメモリなので、constructor でなく代入（assigner）を使う。代入できない型は factory で作り直す

        for (auto *op : ops)
        {
            // 値の上書きだけを扱うので AddComponent 以外は無視
            if (op->kind != PendingOpKind::AddComponent) continue;

            void *dstPtr = srcChunk->GetComponentPtrByType(op->type, srcIndex);
            if (!dstPtr || (!op->factory && !op->data)) continue;

            const Archetype &arch = srcChunk->GetArchetype();
            for (const auto &td : arch)
            {
                if (td.id != op->type) continue;

                // ムーブ専用型など、代入できない型
                if (op->factory)
                {
                    td.destructor.Destruct(dstPtr);
                    op->factory(dstPtr);
                }
                else if (op->data)
                {
                    if (td.assigner)
                    {
                        td.assigner(dstPtr, op->data);
                    }
                    else
                    {
                        std::memcpy(dstPtr, op->data, td.typeSize);
                    }
                }
                break;
            }
        }
    }



    void ChunkManager::ApplyPatchOps(
        EntityID                        entityID,
        const std::vector<PendingOp *> &ops)
    {
        // 最後に回すのは、引っ越しでアドレスが変わるため。先に書くと、捨てる古いスロットに書いてしまう

        // 引っ越し後の最終的なスロットを引き直す
        size_t finalIndex    = 0;
        size_t finalChunkIdx = SearchEntityIn(entityID, &finalIndex);
        if (finalChunkIdx == InvalidIndex) return;

        Chunk *finalChunk = m_chunks[finalChunkIdx].get();

        for (auto *op : ops)
        {
            if (op->kind != PendingOpKind::PatchComponent) continue;

            void *ptr = finalChunk->GetComponentPtrByType(op->type, finalIndex);
            if (ptr && op->patcher)
            {
                op->patcher(ptr);
            }
        }
    }



    void ChunkManager::CompactAllChunks()
    {
        // フレームの最後に1回だけ呼ばれる
        // CompactHoles は要素を動かすたびにこのラムダを呼ぶ。ここで住所録を直さないと、
        // SearchEntityIn が古いスロットを返す

        for (auto &c : m_chunks)
        {
            c->CompactHoles([this](EntityID entity, Chunk *chunk, size_t newIndex) {
                size_t chunkIdx = GetChunkIndex(chunk);
                UpdateEntityLocation(entity, chunkIdx, newIndex);

                if (m_onEntityMove)
                {
                    m_onEntityMove(entity, chunk, newIndex);
                }
            });
        }
    }

} // namespace CCL::ECS
