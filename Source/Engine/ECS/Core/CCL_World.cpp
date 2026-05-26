/**
 * @file   CCL_World.cpp
 * @brief  Worldクラスの実装
 *
 * @details
 *  Entity発行/再利用、PendingOpへの予約登録、ScrutinyAndApplyの実装を含みます。
 *  FrameAllocatorのライフタイムに関する旧バグ(spawnArchetypeのダングリング)は
 *  PendingOp::spawnArchetype を値で保持することで解消済みです。
 *
 * @author CCL ECS Library
 */
#include "CCL_World.h"

#include <algorithm>
#include <cassert>
#include <cstring>


namespace CCL::ECS::Core
{
    using namespace CCL::ECS;


    // =========================================================================
    //  ライフサイクル
    // =========================================================================

    World::World()
    {
        _entityGenerations.resize(1024, 1u);
        _nextUnusedIndex = 1;
    }


    World::~World()
    {
        _chunkManager.FinalizeAll();
        _pendingOps.Clear();
    }


    void World::Clear()
    {
        _pendingOps.Clear();
        _chunkManager.FinalizeAll();
        _eventBus.Clear();

        _entityGenerations.clear();
        _freeIndices.clear();
        _nextUnusedIndex = 1;

        // リソースは明示的に再登録される設計のため、ここでは消さない
        // (必要なら _resources.clear() を呼んでください)
    }


    // =========================================================================
    //  Spawn / Destroy
    // =========================================================================

    EntityID World::RequestSpawnEntity(const Archetype& archetype)
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        if (!_freeIndices.empty()) {
            index = _freeIndices.back();
            _freeIndices.pop_back();
            generation = _entityGenerations[index];
        }
        else {
            index = _nextUnusedIndex++;
            if (index >= _entityGenerations.size()) {
                _entityGenerations.resize(index + 1024, 1u);
            }
            generation = 1u;
            _entityGenerations[index] = 1u;
        }

        const EntityID id = EntityHandle::Combine(index, generation);

        PendingOp op;
        op.kind           = PendingOpKind::Spawn;
        op.entity         = id;
        op.spawnArchetype = archetype; // 値保持(FrameAllocator非依存)
        _pendingOps.Add(std::move(op));

        return id;
    }


    void World::RequestDestroyEntity(EntityID entity)
    {
        PendingOp op;
        op.kind   = PendingOpKind::Destroy;
        op.entity = entity;
        _pendingOps.Add(std::move(op));
    }


    void World::RecycleEntityID(EntityID id)
    {
        const std::uint32_t index = EntityHandle::GetIndex(id);
        if (index >= _entityGenerations.size()) return;

        // 世代インクリメント(0はInvalidなので避ける)
        if (_entityGenerations[index] == UINT32_MAX) {
            _entityGenerations[index] = 1u;
        } else {
            ++_entityGenerations[index];
        }
        _freeIndices.push_back(index);
    }


    // =========================================================================
    //  Component予約
    // =========================================================================

    void World::RequestAddComponent(
        EntityID                          entity,
        TypeID                            tid,
        std::size_t                       size,
        const std::function<void(void*)>& ctor,
        const Destructor&                 dtor,
        TypeData::ConstructFunc           copyCtor,
        TypeData::AssignFunc              assigner,
        TypeData::MoveFunc                mover,
        const char*                       name)
    {
        if (!IsEntityValid(entity)) return;

        PendingOp op;
        op.kind        = PendingOpKind::AddComponent;
        op.entity      = entity;
        op.type        = tid;
        op.typeSize    = size;
        op.destructor  = dtor;
        op.constructor = copyCtor;
        op.assigner    = assigner;
        op.mover       = mover;
        op.name        = name;

        if (ctor) {
            op.factory = ctor;
            op.data    = nullptr;
        } else {
            // フォールバック: FrameAllocator上に0クリアした生メモリを置く
            void* mem = _frameAllocator.Alloc(size);
            std::memset(mem, 0, size);
            op.data    = mem;
            op.factory = nullptr;
        }

        _pendingOps.Add(std::move(op));
    }


    void World::RequestRemoveComponent(EntityID entity, TypeID tid)
    {
        if (!IsEntityValid(entity)) return;

        PendingOp op;
        op.kind     = PendingOpKind::RemoveComponent;
        op.entity   = entity;
        op.type     = tid;
        op.typeSize = 0;
        _pendingOps.Add(std::move(op));
    }


    // =========================================================================
    //  ScrutinyAndApply: フレーム末の構造変更
    // =========================================================================

    void World::ScrutinyAndApply()
    {
        const bool hadOps = !_pendingOps.Empty();

        std::vector<EntityID> dead = _chunkManager.ApplyPendingOperations(_pendingOps);

        for (EntityID id : dead) {
            RecycleEntityID(id);
        }

        _pendingOps.Clear();
        _frameAllocator.Reset();

        // 構造変更があった時だけフラグを立てる
        if (hadOps) _isStructureDirty = true;
    }


    // =========================================================================
    //  問い合わせ
    // =========================================================================

    const Archetype& World::GetEntityArchetype(EntityID entity) const
    {
        std::size_t entityIdx = 0;
        const std::size_t chunkIdx = _chunkManager.SearchEntityIn(entity, &entityIdx);

        static const Archetype emptyArchetype;
        if (chunkIdx == InvalidIndex) return emptyArchetype;

        return _chunkManager.GetChunks()[chunkIdx]->GetArchetype();
    }


    void* World::GetComponentOfEntity(EntityID entity, TypeID tid)
    {
        if (!IsEntityValid(entity)) return nullptr;

        std::size_t entityIdx = 0;
        const std::size_t chunkIdx = _chunkManager.SearchEntityIn(entity, &entityIdx);
        if (chunkIdx == InvalidIndex) return nullptr;
        return _chunkManager.GetChunks()[chunkIdx]->GetComponentPtrByType(tid, entityIdx);
    }

} // namespace CCL::ECS::Core
