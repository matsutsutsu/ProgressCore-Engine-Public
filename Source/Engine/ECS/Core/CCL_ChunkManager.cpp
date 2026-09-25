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

    // EntityID から世代を捨て、住所録の添字に使う index を取り出す。
    //
    // 世代の照合はしない。それは World::IsEntityValid の担当。
    // ビットレイアウトの定義が二箇所に散るのを避けるため、0xFFFFFFFF を
    // 直に書かず EntityHandle::GetIndex を通している。
    inline size_t GetPureIndex(EntityID id)
    {
        return static_cast<size_t>(EntityHandle::GetIndex(id));
    }

    // ============================================================
    // 即時実行 API
    // ============================================================

    // 「空きがあるか」は見ない。空きまで保証したいときは GetOrCreateAvailableChunk を使う
    void ChunkManager::CreateChunk(const Archetype &archetype)
    {
        for (auto &c : m_chunks)
        {
            if (c->GetArchetype() == archetype)
                return;
        }
        m_chunks.emplace_back(std::make_unique<Chunk>());

        size_t capacity = CalculateChunkCapacity(archetype);
        m_chunks.back()->Create(archetype, capacity);
    }

    // 構造そのものを壊す操作なので、共有ロックではなく unique_lock を取る
    void ChunkManager::FinalizeAll()
    {
        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        for (auto &c : m_chunks)
            c->Finalize();
        m_chunks.clear();

        m_entityLocationMap.clear();
    }

    // 予約を経由しない【即時実行】ルート。ゲームコードからの Spawn はここへ来ず、
    // PendingOps → ApplyPendingOperations → InsertNewEntity を通る。
    // こちらは初期値を書き込まないので、スロットはゼロクリアのままになる。
    void ChunkManager::AddEntity(EntityID entity, const Archetype &archetype)
    {
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

    // デストラクタまで面倒を見るのは予約経由の ApplyDestroyOp のほう。
    // RAII を持つコンポーネントがある Entity をこちらで消すとリークするので、
    // ゲームコードからは World::RequestDestroyEntity を使うこと。
    void ChunkManager::DestroyEntity(EntityID entity)
    {
        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        size_t entityIndex;
        size_t chunkIndex = SearchEntityIn(entity, &entityIndex);
        if (chunkIndex != InvalidIndex)
        {
            m_chunks[chunkIndex]->MarkDestroyEntity(entityIndex);
            RemoveEntityLocation(entity);
        }
    }

    // 世代チェックをしない。古い世代の ID を渡すと、index を再利用した
    // 【別の】Entity の住所が返る。照合は World::IsEntityValid の担当で、
    // World::GetComponent はそちらを通してからここへ来る。
    //
    // chunkIndex == InvalidIndex は「墓石」。RemoveEntityLocation は要素を
    // erase せず無効値で上書きするため（添字が index と対応している以上、
    // 途中を削ると後ろの住所が全部ずれる）。
    size_t ChunkManager::SearchEntityIn(EntityID entity, size_t *outEntityIndex) const
    {
        size_t index = GetPureIndex(entity);

        if (index >= m_entityLocationMap.size()) return InvalidIndex;

        const auto &loc = m_entityLocationMap[index];

        // 墓石（破棄済みでスロットが無い）
        if (loc.chunkIndex == InvalidIndex) return InvalidIndex;

        if (outEntityIndex) *outEntityIndex = loc.entityIndex;
        return loc.chunkIndex;
    }

    // 無ければ作るので戻り値は必ず有効（参照で返しているのはこのため。
    // 呼び出し側に nullptr チェックは要らない）。
    //
    // 同じ型構成の2個目以降の Chunk が生まれるのはここ。検索条件に
    // GetEntityCount() < GetCapacity() が入っているので、定員に達した Chunk は弾かれる。
    Chunk &ChunkManager::GetOrCreateAvailableChunk(const Archetype &archetype)
    {
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

    // addOps を【逆順】に走査するのは、同じ型へ Set を複数回呼んだとき
    // 「最後の値を勝たせる」仕様のため。後ろから見て最初の一致で break する。
    //
    // 構築の優先順位が値の壊れ方を決める:
    //   factory > mover > constructor > memcpy
    // mover を constructor より先に見るのが要点。ここでコピー構築すると、
    // 後段の PendingOps::Clear が元データに destructor を呼んだ時点で
    // RAII リソース（ModelInstanceHandle 等）が解放され、複製先が死ぬ。
    void ChunkManager::InsertNewEntity(
        Chunk                          &dstChunk,
        EntityID                        entityID,
        const std::vector<PendingOp *> &addOps)
    {
        size_t newIndex = dstChunk.AddEntity(entityID);

        size_t chunkIdx = GetChunkIndex(&dstChunk);
        UpdateEntityLocation(entityID, chunkIdx, newIndex);

        for (const TypeData &td : dstChunk.GetTypeDatas())
        {
            void *dstPtr = dstChunk.GetComponentPtrByType(td.id, newIndex);
            if (!dstPtr) continue;

            bool filled = false;

            // 逆順＝後から積まれた AddComponent の値を勝たせる
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
                    // 一時メモリからの移送は必ずムーブを先に試す。
                    // PendingOp::data は非const の void* なので mover(void*, void*) へそのまま渡せる。
                    if (op->data && td.mover)
                    {
                        td.mover(dstPtr, op->data); // 元は抜け殻になる（＝Clear で壊されても安全）
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

            // 初期値の指定が無い型はデフォルト構築、それも無ければゼロクリア
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

    // スロットが動く操作（生成・引っ越し・穴詰め）は必ずここを通すこと。
    // 1箇所でも漏れると SearchEntityIn が古いスロットを返し、
    // 別の Entity のデータを掴む形で壊れる。
    void ChunkManager::UpdateEntityLocation(EntityID entity, size_t chunkIdx, size_t entityIdx)
    {
        size_t index = GetPureIndex(entity);

        if (index >= m_entityLocationMap.size())
        {
            m_entityLocationMap.resize(index + 1);
        }

        m_entityLocationMap[index] = EntityLocation(chunkIdx, entityIdx);
    }

    // erase せず無効値で上書きして「墓石」を残す。
    // m_entityLocationMap は index を配列の添字にして引く設計なので、
    // 途中の要素を erase すると後ろが全部前へずれ、他の全 Entity の住所が壊れる。
    void ChunkManager::RemoveEntityLocation(EntityID entity)
    {
        size_t index = GetPureIndex(entity);

        if (index < m_entityLocationMap.size())
        {
            m_entityLocationMap[index] = EntityLocation(InvalidIndex, InvalidIndex);
        }
    }

    // 【線形探索】。Chunk 数に比例して遅くなり、引っ越しの多いフレームでは
    // O(移動数 × Chunk数) になる。既知の弱点で、必要になったら
    // Chunk 側に自分の添字を持たせて潰す。
    size_t ChunkManager::GetChunkIndex(const Chunk *chunk) const
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (m_chunks[i].get() == chunk)
            {
                return i;
            }
        }
        return InvalidIndex;
    }

    // Chunk のサイズを 16KB 固定にしているのは、Chunk ごと CPU キャッシュに載せるため。
    // 定員はコンポーネント構成で変わる（例: Position+Velocity=24B なら 682体）。
    //
    // 例外が2つ。タグ型だけで stride==0 のときは割れないので 1024、
    // 1体が 16KB を超えるときは 0 になってしまうので最低 1 を返す。
    size_t ChunkManager::CalculateChunkCapacity(const Archetype &archetype) const
    {
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

    // 引っ越し先の各列は3パターンで埋まる:
    //   A: 元の Chunk にもある型 → mover で運ぶ。引っ越しても値が保持されるのはここ
    //   B: 新しく足された型      → addOps の初期値で構築
    //   C: どちらでもない        → ゼロクリア
    //
    // 住所録の更新をデータ移送より【先に】やっている。以降の
    // GetComponentPtrByType が新しいスロットを前提にしているため。
    //
    // 元のスロットは MarkDestroyEntity で印を付けるだけ。実際に詰まるのは
    // フレーム末尾の CompactAllChunks。
    bool ChunkManager::MoveEntityBetweenChunks(
        Chunk                          &srcChunk,
        size_t                          srcIndex,
        Chunk                          &dstChunk,
        EntityID                        entity,
        const std::vector<PendingOp *> &addOps,
        const std::vector<TypeID>      &removeTypeIDs)
    {
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
                // パターンC: 指定が無ければゼロクリア
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
    //
    // 呼び出しの構造:
    //   ApplyPendingOperations
    //     ├─ GroupOpsByEntity()
    //     ├─ for each entity: ProcessEntityOps()
    //     │     ├─ HasDestroyOp() / ApplyDestroyOp()
    //     │     ├─ DetermineNextArchetype()
    //     │     ├─ CalculateArchetypeDiff()
    //     │     ├─ InsertNewEntity() / MoveEntityBetweenChunks() / UpdateExistingComponents()
    //     │     └─ ApplyPatchOps()
    //     └─ CompactAllChunks()

    // 穴詰めを Entity ごとにやらず最後の CompactAllChunks へまとめているのは、
    // 1体消すたびに詰めるとスロットがずれるたびに住所録の更新が要るため。
    std::vector<EntityID> ChunkManager::ApplyPendingOperations(PendingOps &pendingOps)
    {
        std::unique_lock<std::shared_mutex> lock(m_structureMutex);

        std::vector<EntityID> destroyedEntities;
        if (pendingOps.Ops().empty()) return destroyedEntities;

        // 先に Entity 単位へ束ね直す。予約は積まれた順に一列なので同じ Entity への
        // 操作が散らばっており、そのまま処理すると「コンポーネントを足すたびに引っ越し」
        // が起きる。まとめておけば最終的な型構成を一度で計算でき、引っ越しは1回で済む。
        auto opsByEntity = GroupOpsByEntity(pendingOps);

        // 多重破棄の防止。存在確認を何度も引くので O(1) の set で持つ。
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

    // 要はステップ5の3分岐で、「型構成が変わったか」だけで引っ越すか否かが決まる。
    //   Spawn            → InsertNewEntity（新しくスロットを作る）
    //   型構成が変わった → MoveEntityBetweenChunks（引っ越し）
    //   型構成が同じ     → UpdateExistingComponents（値の上書きだけ）
    // 同じ型へ値を入れ直すだけなら引っ越しは起きないので、こちらは十分に速い。
    void ChunkManager::ProcessEntityOps(
        EntityID                        entityID,
        const std::vector<PendingOp *> &ops,
        std::unordered_set<EntityID>   &alreadyDestroyed,
        std::vector<EntityID>          &destroyedEntities)
    {
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

        // 以下2つは防御的チェック。opsByEntity は EntityID がキーで1体につき1回しか
        // ここへ来ず、MarkDestroyEntity を呼ぶ3箇所（DestroyEntity / ApplyDestroyOp /
        // MoveEntityBetweenChunks）はいずれも住所録を先に更新するため、現状この分岐は
        // 踏まれない。破棄判定を1箇所に頼らないための保険として残している。
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

    // 他の判定より先に呼ばれ、true なら残りの予約はすべて捨てられる
    bool ChunkManager::HasDestroyOp(const std::vector<PendingOp *> &ops) const
    {
        for (auto *op : ops)
        {
            if (op->kind == PendingOpKind::Destroy)
            {
                return true;
            }
        }
        return false;
    }

    // destructor は呼ぶのにスロットは MarkDestroyEntity で印を付けるだけなのは、
    // 「中身の破棄」と「スロットの詰め直し」を分離しているため。詰めるのは
    // フレーム末尾の CompactAllChunks の仕事。
    //
    // Chunk は生メモリなので、破棄は TypeData が持つ destructor の
    // 関数ポインタ経由で行う（型消去）。ここを飛ばすと RAII を持つ
    // コンポーネントがリークする。
    void ChunkManager::ApplyDestroyOp(
        EntityID                      entityID,
        std::unordered_set<EntityID> &alreadyDestroyed,
        std::vector<EntityID>        &destroyedEntities)
    {
        if (alreadyDestroyed.find(entityID) != alreadyDestroyed.end())
        {
            return;
        }

        size_t entityIndex = 0;
        size_t chunkIdx    = SearchEntityIn(entityID, &entityIndex);
        if (chunkIdx == InvalidIndex) return; // 既に居ない

        Chunk *cptr = m_chunks[chunkIdx].get();

        // 既に印が付いている＝別の経路（即時の DestroyEntity や引っ越し）で処理済み。
        // ここで destructor を呼ぶと二重破棄になるので、記録だけして抜ける
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

    // spawnArchetype が PendingOp の【値】メンバなのは、参照で持つと
    // FrameAllocator の寿命が切れた瞬間にダングリングするため。
    // 経緯は World::RequestSpawnEntity のコメント。
    bool ChunkManager::DetermineNextArchetype(
        const std::vector<PendingOp *> &ops,
        Chunk                          *srcChunk,
        Archetype                      &outArchetype,
        bool                           &outIsNewSpawn) const
    {
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

    // AddComponent は、既にその型を持っていても outAddOps へ【必ず】積む。
    // 型構成が変わらない場合の「値の上書き」にも同じリストを使うため。
    //
    // 型を新設するときは destructor/constructor/assigner/mover の関数ポインタも
    // 一緒に TypeData へ詰める。ここが欠けると型消去の経路が全部 memcpy へ
    // 落ち、RAII を持つコンポーネントが壊れる。
    void ChunkManager::CalculateArchetypeDiff(
        const std::vector<PendingOp *> &ops,
        Archetype                      &nextArch,
        std::vector<PendingOp *>       &outAddOps,
        std::vector<TypeID>            &outRemoveIDs) const
    {
        for (auto *op : ops)
        {
            if (op->kind == PendingOpKind::AddComponent)
            {
                outAddOps.push_back(op);

                // Archetype は内部の std::vector<TypeData> に begin()/end() を公開しているので
                // 範囲 for で回せる（CCL_Archetype.h）
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
                    // PendingOp が型情報を運んでくるのは、AddComponent<T> のテンプレートを
                    // 通った時点でしか T の関数ポインタを取れないため。ここで詰め直す
                    TypeData newType;
                    newType.id          = op->type;
                    newType.typeSize    = op->typeSize;
                    newType.destructor  = op->destructor;
                    newType.constructor = op->constructor;
                    newType.assigner    = op->assigner;
                    newType.mover       = op->mover;
                    newType.name        = op->name;

                    // Archetype は TypeID 昇順を保つ決まりなので、Add/Sub は
                    // 内部で二分探索して正しい位置へ挿入・削除する
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

    // 呼ばれるのは ProcessEntityOps のステップ5で「最終形 == 今の型構成」だったとき
    // （例: 既に Position を持つ Entity へもう一度 Position を Set した）。
    //
    // ここは【既に構築済みのメモリ】なので、新規構築の経路とは使う関数が違う。
    // constructor は未構築の場所にしか使えないため、代わりに assigner（代入演算子）を使う。
    // ムーブ専用型など代入できない型は factory 経由で「破棄 → 再構築」する。
    void ChunkManager::UpdateExistingComponents(
        Chunk *srcChunk, size_t srcIndex,
        const std::vector<PendingOp *> &ops)
    {
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

    // これが一番最後なのは、引っ越しが起きるとアドレスが変わるため。先に patch すると
    // これから捨てられる古いスロットを書いてしまうので、移動が全部終わってから
    // SearchEntityIn で【最終的な場所】を引き直す。
    //
    // op->patcher は void* を取るラムダ。型安全なラムダをこの形へ包むのは
    // World::PatchComponent の側。
    void ChunkManager::ApplyPatchOps(
        EntityID                        entityID,
        const std::vector<PendingOp *> &ops)
    {
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

    // フレーム末尾に1回だけ回る。
    //
    // 渡しているラムダが本体。CompactHoles は要素を動かすたびにこれを呼ぶので、
    // その中で住所録を更新し、外部のフック（m_onEntityMove）にも知らせる。
    // 住所録の更新を落とすと、詰めた後に SearchEntityIn が古いスロットを返し、
    // 別の Entity のデータを掴む形で壊れる。
    void ChunkManager::CompactAllChunks()
    {
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
