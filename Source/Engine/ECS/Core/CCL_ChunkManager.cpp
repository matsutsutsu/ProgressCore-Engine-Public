#include "CCL_ChunkManager.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <cassert>

namespace CCL::ECS
{
    using namespace CCL::ECS;

    // ----------------------------------------------------------------
    // 内部ユーティリティ
    // ----------------------------------------------------------------

    /// @brief EntityID から世代情報を切り捨て、純粋なインデックスを取り出す
    inline size_t GetPureIndex(EntityID id)
    {
        return static_cast<size_t>(id & 0xFFFFFFFF);
    }

    // ================================================================
    // 公開 API
    // ================================================================

    void ChunkManager::CreateChunk(const Archetype& archetype)
    {
        for (auto& c : _chunks)
        {
            if (c->GetArchetype() == archetype)
                return;
        }
        _chunks.emplace_back(std::make_unique<Chunk>());

        // アーキタイプから計算した定員を渡す
        size_t capacity = CalculateChunkCapacity(archetype);
        _chunks.back()->Create(archetype, capacity);
    }

    void ChunkManager::FinalizeAll()
    {
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        for (auto& c : _chunks)
            c->Finalize();
        _chunks.clear();

        _entityLocationMap.clear();
    }

    void ChunkManager::AddEntity(EntityID entity, const Archetype& archetype)
    {
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        // 1. 同 Archetype かつ空きがある Chunk を探す
        auto it = std::find_if(_chunks.begin(), _chunks.end(),
            [&](const std::unique_ptr<Chunk>& c) {
                return c->GetArchetype() == archetype &&
                       c->GetEntityCount() < c->GetCapacity();
            });

        // 2. 無ければ新規作成
        if (it == _chunks.end())
        {
            _chunks.emplace_back(std::make_unique<Chunk>());
            size_t capacity = CalculateChunkCapacity(archetype);
            _chunks.back()->Create(archetype, capacity);
            it = std::prev(_chunks.end());
        }

        // 3. 挿入
        (*it)->InsertEntity(entity);

        // 4. 位置を登録
        size_t chunkIdx  = std::distance(_chunks.begin(), it);
        size_t entityIdx = (*it)->GetEntityCount() - 1;
        UpdateEntityLocation(entity, chunkIdx, entityIdx);
    }

    void ChunkManager::DestroyEntity(EntityID entity)
    {
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        size_t entityIndex;
        size_t chunkIndex = SearchEntityIn(entity, &entityIndex);
        if (chunkIndex != InvalidIndex)
        {
            _chunks[chunkIndex]->MarkDestroyEntity(entityIndex);
            RemoveEntityLocation(entity);
        }
    }

    size_t ChunkManager::SearchEntityIn(EntityID entity, size_t* outEntityIndex) const
    {
        size_t index = GetPureIndex(entity);

        if (index >= _entityLocationMap.size()) return InvalidIndex;

        // O(1) のキャッシュヒット確定アクセス
        const auto& loc = _entityLocationMap[index];

        // 墓石（無効化済み）
        if (loc.chunkIndex == InvalidIndex) return InvalidIndex;

        if (outEntityIndex) *outEntityIndex = loc.entityIndex;
        return loc.chunkIndex;
    }

    Chunk& ChunkManager::GetOrCreateAvailableChunk(const Archetype& archetype)
    {
        auto it = std::find_if(_chunks.begin(), _chunks.end(),
            [&](const std::unique_ptr<Chunk>& c) {
                return c->GetArchetype() == archetype &&
                       c->GetEntityCount() < c->GetCapacity();
            });

        if (it == _chunks.end())
        {
            _chunks.emplace_back(std::make_unique<Chunk>());
            size_t capacity = CalculateChunkCapacity(archetype);
            _chunks.back()->Create(archetype, capacity);
            return *_chunks.back();
        }
        return **it;
    }

    void ChunkManager::InsertNewEntity(
        Chunk& dstChunk,
        EntityID entityID,
        const std::vector<PendingOp*>& addOps)
    {
        // 1. 席を確保して、正しいインデックスを直接受け取る
        size_t newIndex = dstChunk.AddEntity(entityID);

        // 位置を登録
        size_t chunkIdx = GetChunkIndex(&dstChunk);
        UpdateEntityLocation(entityID, chunkIdx, newIndex);

        // 2. チャンクの全構成要素を初期化
        for (const TypeData& td : dstChunk.GetTypeDatas())
        {
            void* dstPtr = dstChunk.GetComponentPtrByType(td.id, newIndex);
            if (!dstPtr) continue;

            bool filled = false;

            // 3. PendingOp に該当する初期データがあるか探す
            // 最後にした AddComponent の値で上書きする仕様のため逆順ループ
            for (int i = (int)addOps.size() - 1; i >= 0; --i)
            {
                auto* op = addOps[i];
                if (op->type == td.id)
                {
                    // factory があれば直接構築 (Emplace)
                     // コピー禁止型もここで Move 構築される
                    if (op->factory) {
                        op->factory(dstPtr);
                    }
                    // ★ 修正: 一時メモリからの移送はムーブを最優先する。
                    //    （元の構造を保ち、constructor の if だけを mover 優先に変更）
                    //    mover があれば op->data を「抜け殻」にしてムーブ構築。
                    //    これにより後段の PendingOps::Clear で元を destructor しても
                    //    RAII リソース（ModelInstanceHandle 等）が解放されない。
                    if (op->data && td.mover)
                    {
                        td.mover(dstPtr, op->data);       // ムーブ構築（元は無効化される）
                    }
                    else if (op->data && td.constructor)
                    {
                        td.constructor(dstPtr, op->data); // フォールバック: コピー構築
                    }
                    else if (op->data)
                    {
                        std::memcpy(dstPtr, op->data, td.typeSize);
                    }
                    filled = true;
                    break;


                    //  ----------------------------------------------------------------------------
                    //  補足: op->data の型について
                    //    PendingOp::data は void*（非const）。td.mover の引数も (void* dst, void* src)
                    //    なのでそのまま渡せる。constructor は (void*, const void*) だが、現状の
                    //    呼び出しは op->data を const として渡しているだけで実体は非const。問題なし。
                    //
                    //  補足: MoveEntityBetweenChunks（チャンク間移動）側は既に 297 行目で
                    //    tdDst.mover を優先しているため修正不要。今回の不具合は「新規 AddComponent
                    //    適用」経路（InsertNewEntity）だけが constructor を使っていたことが原因。
                    //============================================================================
                }
            }

            // 4. データが無ければデフォルト構築 or ゼロクリア
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

        // 5. 生成完了の通知
        if (_onEntityMove) {
            _onEntityMove(entityID, &dstChunk, newIndex);
        }
    }

    size_t ChunkManager::SearchEqualChunk(const Archetype& a) const
    {
        for (size_t i = 0; i < _chunks.size(); ++i)
        {
            if (_chunks[i]->GetArchetype() == a)
                return i;
        }
        return InvalidIndex;
    }

    void ChunkManager::UpdateEntityLocation(EntityID entity, size_t chunkIdx, size_t entityIdx)
    {
        size_t index = GetPureIndex(entity);

        if (index >= _entityLocationMap.size()) {
            _entityLocationMap.resize(index + 1);
        }

        _entityLocationMap[index] = EntityLocation(chunkIdx, entityIdx);
    }

    void ChunkManager::RemoveEntityLocation(EntityID entity)
    {
        size_t index = GetPureIndex(entity);

        if (index < _entityLocationMap.size()) {
            // erase ではなく無効値で上書き（再利用時の整合性のため）
            _entityLocationMap[index] = EntityLocation(InvalidIndex, InvalidIndex);
        }
    }

    size_t ChunkManager::GetChunkIndex(const Chunk* chunk) const
    {
        for (size_t i = 0; i < _chunks.size(); ++i) {
            if (_chunks[i].get() == chunk) {
                return i;
            }
        }
        return InvalidIndex;
    }

    size_t ChunkManager::CalculateChunkCapacity(const Archetype& archetype) const
    {
        size_t stride = 0;
        for (const auto& t : archetype) {
            stride += t.typeSize;
        }

        // 例外1: タグのみ（サイズ 0）
        if (stride == 0) return 1024;

        // 例外2: 1 体が大きすぎて 16KB に収まらない場合、最低 1 体は確保
        size_t capacity = CHUNK_SIZE_BYTES / stride;
        return capacity > 0 ? capacity : 1;
    }

    // ================================================================
    // Archetype 遷移（既存メソッド・挙動は元のまま）
    // ================================================================
    bool ChunkManager::MoveEntityBetweenChunks(
        Chunk& srcChunk,
        size_t srcIndex,
        Chunk& dstChunk,
        EntityID entity,
        const std::vector<PendingOp*>& addOps,
        const std::vector<TypeID>& removeTypeIDs)
    {
        // 既に削除されていれば移動不可
        if (srcChunk.IsEntityDestroyed(srcIndex)) {
            return false;
        }

        // 1. 宛先 Chunk に席を確保
        dstChunk.InsertEntity(entity);
        size_t dstIndex = dstChunk.GetEntityCount() - 1;

        // 新しい住所をマスターマップに登録（重要）
        size_t dstChunkIdx = GetChunkIndex(&dstChunk);
        UpdateEntityLocation(entity, dstChunkIdx, dstIndex);

        const Archetype& srcArch = srcChunk.GetArchetype();
        const Archetype& dstArch = dstChunk.GetArchetype();

        // TypeID → インデックスの逆引きマップを作成
        auto BuildIndexMap = [](const Archetype& a) {
            std::unordered_map<TypeID, size_t> m;
            for (size_t i = 0; i < a.size(); ++i) m[a[i].id] = i;
            return m;
        };
        auto srcMap = BuildIndexMap(srcArch);
        auto dstMap = BuildIndexMap(dstArch);

        // 2. 宛先 Chunk の各列を順に埋める
        for (size_t di = 0; di < dstArch.size(); ++di)
        {
            const TypeData& tdDst = dstArch[di];
            void* dstPtr = dstChunk.GetComponentPtrByType(tdDst.id, dstIndex);

            // パターンA: 元の家からそのまま持ってくる
            auto itSrc = srcMap.find(tdDst.id);
            if (itSrc != srcMap.end())
            {
                void* srcPtr = srcChunk.GetComponentPtrByType(tdDst.id, srcIndex);

                if (tdDst.mover) {
                    tdDst.mover(dstPtr, srcPtr);
                }
                else if (tdDst.constructor) {
                    tdDst.constructor(dstPtr, srcPtr);
                }
                else {
                    std::memcpy(dstPtr, srcPtr, tdDst.typeSize);
                }
            }
            // パターンB: 新規追加された荷物
            else
            {
                bool filled = false;
                for (PendingOp* op : addOps) {
                    if (op->type == tdDst.id) {
                        if (op->factory) {
                            op->factory(dstPtr);
                            filled = true;
                            break;
                        }
                        else if (op->data) {
                            if (tdDst.constructor)
                                tdDst.constructor(dstPtr, op->data);
                            else
                                std::memcpy(dstPtr, op->data, tdDst.typeSize);
                            filled = true;
                            break;
                        }
                    }
                }
                // パターンC: 何も指定がなければ 0 クリア
                if (!filled)
                {
                    std::memset(dstPtr, 0, tdDst.typeSize);
                }
            }
        }

        // 元 Chunk のデータを全て破棄
        for (size_t si = 0; si < srcArch.size(); ++si)
        {
            const TypeData& tdSrc = srcArch[si];
            void* srcPtr = srcChunk.GetComponentPtrByType(tdSrc.id, srcIndex);
            tdSrc.destructor.Destruct(srcPtr);
        }

        // 引っ越し完了を通知
        if (_onEntityMove) {
            _onEntityMove(entity, &dstChunk, dstIndex);
        }

        // 元 Chunk のエンティティを削除マーク
        srcChunk.MarkDestroyEntity(srcIndex);

        return true;
    }


    // ================================================================
    // ApplyPendingOperations - 分割版オーケストレータ
    // ================================================================
    //
    // 設計方針:
    //   かつて 280 行あった巨大関数を以下の責務に分解しています。
    //   挙動は完全に元と同一を保証しています。
    //
    //   ApplyPendingOperations (本関数)
    //     ├─ GroupOpsByEntity()
    //     ├─ for each entity:
    //     │   └─ ProcessEntityOps()
    //     │       ├─ HasDestroyOp()
    //     │       ├─ ApplyDestroyOp()
    //     │       ├─ DetermineNextArchetype()
    //     │       ├─ CalculateArchetypeDiff()
    //     │       ├─ UpdateExistingComponents()
    //     │       └─ ApplyPatchOps()
    //     └─ CompactAllChunks()
    // ================================================================

    std::vector<EntityID> ChunkManager::ApplyPendingOperations(PendingOps& pendingOps)
    {
        // 排他ロックを取得（この間 System は待機）
        std::unique_lock<std::shared_mutex> lock(_structureMutex);

        std::vector<EntityID> destroyedEntities;
        if (pendingOps.Ops().empty()) return destroyedEntities;

        // Phase 1: PendingOps を Entity ごとに束ねる
        auto opsByEntity = GroupOpsByEntity(pendingOps);

        // Phase 2: 各 Entity の操作を処理
        std::unordered_set<EntityID> alreadyDestroyed;
        for (auto& [entityID, ops] : opsByEntity)
        {
            ProcessEntityOps(entityID, ops, alreadyDestroyed, destroyedEntities);
        }

        // Phase 3: 全 Chunk の穴詰め
        CompactAllChunks();

        return destroyedEntities;
    }


    // ----------------------------------------------------------------
    // Phase 1: PendingOps を Entity ID ごとにグループ化
    // ----------------------------------------------------------------
    std::unordered_map<EntityID, std::vector<PendingOp*>>
    ChunkManager::GroupOpsByEntity(PendingOps& pendingOps)
    {
        std::unordered_map<EntityID, std::vector<PendingOp*>> opsByEntity;
        for (auto& op : pendingOps.Ops()) {
            opsByEntity[op.entity].push_back(&op);
        }
        return opsByEntity;
    }


    // ----------------------------------------------------------------
    // Phase 2: 1 Entity 分の処理オーケストレータ
    // ----------------------------------------------------------------
    void ChunkManager::ProcessEntityOps(
        EntityID entityID,
        const std::vector<PendingOp*>& ops,
        std::unordered_set<EntityID>& alreadyDestroyed,
        std::vector<EntityID>& destroyedEntities)
    {
        // -------------------------------------------------------
        // ステップ 1: 破棄(Destroy) の優先チェック
        // -------------------------------------------------------
        if (HasDestroyOp(ops)) {
            ApplyDestroyOp(entityID, alreadyDestroyed, destroyedEntities);
            return; // 破棄が確定したら他の操作は全て無視
        }

        // -------------------------------------------------------
        // ステップ 2: 既存状態の確認と生存チェック
        // -------------------------------------------------------
        size_t srcIndex = 0;
        size_t chunkIdx = SearchEntityIn(entityID, &srcIndex);
        Chunk* srcChunk = (chunkIdx != InvalidIndex) ? _chunks[chunkIdx].get() : nullptr;

        // 削除済みエンティティへの操作をスキップ
        if (alreadyDestroyed.find(entityID) != alreadyDestroyed.end()) {
            return;
        }

        // 既に削除マークがついている場合もスキップ
        if (srcChunk && srcChunk->IsEntityDestroyed(srcIndex)) {
            return;
        }

        // -------------------------------------------------------
        // ステップ 3: 次の Archetype のベースを決定
        // -------------------------------------------------------
        Archetype nextArch;
        bool isNewSpawn = false;
        if (!DetermineNextArchetype(ops, srcChunk, nextArch, isNewSpawn)) {
            return; // 存在しないし Spawn でもないなら何もしない
        }

        // -------------------------------------------------------
        // ステップ 4: Add/Remove 差分の計算と nextArch への反映
        // -------------------------------------------------------
        std::vector<PendingOp*> addOps;
        std::vector<TypeID> removeIDs;
        CalculateArchetypeDiff(ops, nextArch, addOps, removeIDs);

        // -------------------------------------------------------
        // ステップ 5: 最終確定処理
        // -------------------------------------------------------
        if (isNewSpawn) {
            // Spawn: 直接 nextArch を持つ Chunk へ挿入
            Chunk& dstChunk = GetOrCreateAvailableChunk(nextArch);
            InsertNewEntity(dstChunk, entityID, addOps);
        }
        else if (!(nextArch == srcChunk->GetArchetype())) {
            // Archetype 変更: 別 Chunk へ引っ越し
            Chunk& dstChunk = GetOrCreateAvailableChunk(nextArch);

            // MoveEntityBetweenChunks 前に再度削除チェック
            if (!srcChunk->IsEntityDestroyed(srcIndex)) {
                MoveEntityBetweenChunks(*srcChunk, srcIndex, dstChunk, entityID, addOps, removeIDs);
            }
        }
        else {
            // Archetype が変わらない（既存 Component を上書き）
            UpdateExistingComponents(srcChunk, srcIndex, ops);
        }

        // -------------------------------------------------------
        // ステップ 6: Patch（部分修正）の適用
        // -------------------------------------------------------
        ApplyPatchOps(entityID, ops);
    }


    // ----------------------------------------------------------------
    // ステップ 1 補助: Destroy マーカー検出
    // ----------------------------------------------------------------
    bool ChunkManager::HasDestroyOp(const std::vector<PendingOp*>& ops) const
    {
        for (auto* op : ops) {
            if (op->kind == PendingOpKind::Destroy) {
                return true;
            }
        }
        return false;
    }


    // ----------------------------------------------------------------
    // ステップ 1 補助: Destroy 確定処理
    // ----------------------------------------------------------------
    void ChunkManager::ApplyDestroyOp(
        EntityID entityID,
        std::unordered_set<EntityID>& alreadyDestroyed,
        std::vector<EntityID>& destroyedEntities)
    {
        // 既に削除済みならスキップ
        if (alreadyDestroyed.find(entityID) != alreadyDestroyed.end()) {
            return;
        }

        // 既存エンティティの存在確認
        size_t entityIndex = 0;
        size_t chunkIdx = SearchEntityIn(entityID, &entityIndex);
        if (chunkIdx == InvalidIndex) return; // 存在しないなら何もしない

        Chunk* cptr = _chunks[chunkIdx].get();

        // 既に削除マークがあれば二重処理を回避
        if (cptr->IsEntityDestroyed(entityIndex)) {
            alreadyDestroyed.insert(entityID);
            return;
        }

        // 全コンポーネントのデストラクタを順に呼び出す
        for (const TypeData& td : cptr->GetArchetype()) {
            // td.id 型の entityIndex 番目データの住所を取得
            void* ptr = cptr->GetComponentPtrByType(td.id, entityIndex);

            // その型専用のデストラクタで適切に破棄
            td.destructor.Destruct(ptr);
        }

        // 削除マーク
        cptr->MarkDestroyEntity(entityIndex);

        // 住所録から完全抹消
        RemoveEntityLocation(entityID);

        // 結果リストへ登録
        alreadyDestroyed.insert(entityID);
        destroyedEntities.push_back(entityID);
    }


    // ----------------------------------------------------------------
    // ステップ 3 補助: 次の Archetype を決定
    // ----------------------------------------------------------------
    bool ChunkManager::DetermineNextArchetype(
        const std::vector<PendingOp*>& ops,
        Chunk* srcChunk,
        Archetype& outArchetype,
        bool& outIsNewSpawn) const
    {
        outIsNewSpawn = false;

        // ops の中に Spawn があるか探す
        for (auto* op : ops) {
            if (op->kind == PendingOpKind::Spawn) {
                // spawnArchetype は PendingOp の値メンバなので
                // FrameAllocator のライフタイムに無関係で常に安全
                outArchetype = op->spawnArchetype;
                outIsNewSpawn = true;
                return true;
            }
        }

        // Spawn でないなら、既存 Chunk の Archetype を引き継ぐ
        if (!srcChunk) {
            // 存在しないし Spawn でもない → 何もできない
            return false;
        }
        outArchetype = srcChunk->GetArchetype();
        return true;
    }


    // ----------------------------------------------------------------
    // ステップ 4 補助: Add/Remove 差分計算
    // ----------------------------------------------------------------
    void ChunkManager::CalculateArchetypeDiff(
        const std::vector<PendingOp*>& ops,
        Archetype& nextArch,
        std::vector<PendingOp*>& outAddOps,
        std::vector<TypeID>& outRemoveIDs) const
    {
        for (auto* op : ops) {
            if (op->kind == PendingOpKind::AddComponent) {
                // 「初期値データ」として addOps には必ず入れる
                outAddOps.push_back(op);

                // 既に Archetype に含まれていなければ追加
                bool has = false;
                for (auto& td : nextArch) {
                    if (td.id == op->type) { has = true; break; }
                }
                if (!has) {
                    // 関数ポインタを含めた完全な TypeData を構築して追加
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
                outRemoveIDs.push_back(op->type);
                nextArch.Sub(op->type);
            }
        }
    }


    // ----------------------------------------------------------------
    // ステップ 5 補助: 同一 Archetype 内のコンポーネント値更新
    // ----------------------------------------------------------------
    void ChunkManager::UpdateExistingComponents(
        Chunk* srcChunk, size_t srcIndex,
        const std::vector<PendingOp*>& ops)
    {
        for (auto* op : ops) {
            if (op->kind != PendingOpKind::AddComponent) continue;

            void* dstPtr = srcChunk->GetComponentPtrByType(op->type, srcIndex);
            if (!dstPtr || (!op->factory && !op->data)) continue;

            const Archetype& arch = srcChunk->GetArchetype();
            for (const auto& td : arch) {
                if (td.id != op->type) continue;

                // パターン1: factory がある場合 (Move-Only 型など)
                //   上書き代入ができないため「破棄 → 新規構築」の手順を踏む
                if (op->factory) {
                    td.destructor.Destruct(dstPtr); // 既存を破棄
                    op->factory(dstPtr);            // 新規構築
                }
                // パターン2: data がある場合（従来のコピー可能型）
                else if (op->data) {
                    if (td.assigner) {
                        // 既に構築済みなので代入演算子が安全
                        td.assigner(dstPtr, op->data);
                    }
                    else {
                        std::memcpy(dstPtr, op->data, td.typeSize);
                    }
                }
                break;
            }
        }
    }


    // ----------------------------------------------------------------
    // ステップ 6 補助: Patch（部分書き換え）の適用
    // ----------------------------------------------------------------
    void ChunkManager::ApplyPatchOps(
        EntityID entityID,
        const std::vector<PendingOp*>& ops)
    {
        // 移動・生成が完了した「最終的な場所」を取得
        size_t finalIndex = 0;
        size_t finalChunkIdx = SearchEntityIn(entityID, &finalIndex);
        if (finalChunkIdx == InvalidIndex) return;

        Chunk* finalChunk = _chunks[finalChunkIdx].get();

        for (auto* op : ops) {
            if (op->kind != PendingOpKind::PatchComponent) continue;

            void* ptr = finalChunk->GetComponentPtrByType(op->type, finalIndex);
            if (ptr && op->patcher) {
                op->patcher(ptr);
            }
        }
    }


    // ----------------------------------------------------------------
    // Phase 3: 全 Chunk の穴詰め
    // ----------------------------------------------------------------
    void ChunkManager::CompactAllChunks()
    {
        for (auto& c : _chunks) {
            c->CompactHoles([this](EntityID entity, Chunk* chunk, size_t newIndex) {
                // 位置キャッシュを更新
                size_t chunkIdx = GetChunkIndex(chunk);
                UpdateEntityLocation(entity, chunkIdx, newIndex);

                // 外部コールバック（World 側のキャッシュ更新）も呼ぶ
                if (_onEntityMove) {
                    _onEntityMove(entity, chunk, newIndex);
                }
            });
        }
    }

} // namespace CCL::ECS
