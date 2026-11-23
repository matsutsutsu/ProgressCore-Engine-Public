#include "CCL_World.h"
#include "../System/CCL_SystemManager.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include <algorithm>
#include <cassert>
#include <cstring>

#include "tracy/Tracy.hpp"

namespace CCL::ECS
{
    World::World()
    {
        // index は 1 から配り、0 を無効の意味に使う
        // ゼロ初期化しただけの EntityID を、有効な Entity と取り違えない

        m_entityGenerations.resize(1024, 1);
        m_nextUnusedIndex = 1;
    }



    World::~World()
    {
        m_chunkManager.FinalizeAll();
        m_pendingOps.Clear();
    }



    void World::Clear()
    {
        // 1. 未適用の予約と、全 Chunk のデータを破棄する
        m_pendingOps.Clear();
        m_chunkManager.FinalizeAll();

        // 2. イベントの購読もすべて白紙に戻す
        m_eventBus.Clear();

        // 3. ID 管理情報を初期状態（index は 1 から）に戻す
        m_entityGenerations.clear();
        m_freeIndices.clear();
        m_nextUnusedIndex = 1;

        // シングルトン（m_resourcesArray）は残す
        // Initialize で一度だけ登録するので、ここで消すと登録し直されない
    }



    void World::RequestAddComponent(EntityID                           entity,
                                    TypeID                             tid,
                                    size_t                             size,
                                    const std::function<void(void *)> &ctor,
                                    const Destructor                  &dtor,
                                    TypeData::ConstructFunc            copyCtor,
                                    TypeData::AssignFunc               assigner,
                                    TypeData::MoveFunc                 mover,
                                    const char                        *name)
    {
        // Chunk は生のメモリで型を知らないので、破棄・移動・代入の関数ポインタをデータと一緒に運ぶ
        // 破棄済みの ID への操作は、先頭の IsEntityValid で無視する

        if (!IsEntityValid(entity)) return;

        PendingOp op;
        op.kind       = PendingOpKind::AddComponent;
        op.entity     = entity;
        op.type       = tid;
        op.typeSize   = size;
        op.destructor = dtor;
        // 以下は ChunkManager が TypeData を組むときに使う。nullptr だと memcpy で運ばれる
        op.constructor = copyCtor;
        op.assigner    = assigner;
        op.mover       = mover;
        op.name        = name;

        // コンストラクタはここで実行せず、適用時に構築できるよう factory に保存する
        if (ctor)
        {
            op.factory = ctor;
            op.data    = nullptr;
        }
        else
        {
            // ctor がない場合は、ゼロ埋めした一時データをコピー元として持たせる
            void *mem = m_frameAllocator.Alloc(size);
            std::memset(mem, 0, size);
            op.data    = mem;
            op.factory = nullptr;
        }

        m_pendingOps.Add(op);
    }



    void World::RequestRemoveComponent(EntityID entity, TypeID tid)
    {
        if (!IsEntityValid(entity)) return;

        PendingOp op;
        op.kind     = PendingOpKind::RemoveComponent;
        op.entity   = entity;
        op.type     = tid;
        op.typeSize = 0; // 削除にサイズ情報は不要
        op.data     = nullptr;

        m_pendingOps.Add(op);
    }



    EntityID World::RequestSpawnEntity(const Archetype &archetype)
    {
        // ID を発行して予約を積むだけで、Chunk には触れない（Spawn 直後の GetComponent は nullptr）

        uint32_t index;
        uint32_t generation;

        // 1. 世代付きの ID を発行する
        //    空きの index があれば使い回す（世代は RecycleEntityID で上げてある）。なければ新しく配り、世代は 1
        {
            if (!m_freeIndices.empty())
            {
                index = m_freeIndices.back();
                m_freeIndices.pop_back();
                generation = m_entityGenerations[index];
            }
            else
            {
                index = m_nextUnusedIndex++;
                if (index >= m_entityGenerations.size())
                {
                    m_entityGenerations.resize(index + 1024, 1);
                }
                generation                 = 1;
                m_entityGenerations[index] = 1;
            }
        }

        // 上位32bit=世代 / 下位32bit=index の 64bit ID に合成する
        EntityID id = EntityHandle::Combine(index, generation);

        // 2. 予約に積む。Archetype は値で持たせる
        //    FrameAllocator に置いてポインタで渡すと、Reset の後に破棄済みのメモリを指す
        PendingOp op;
        op.kind           = PendingOpKind::Spawn;
        op.entity         = id;
        op.spawnArchetype = archetype;
        op.data           = nullptr;

        m_pendingOps.Add(op);

        // ID は即座に返す（呼び出し側はすぐ AddComponent 等に使える）
        return id;
    }



    void World::RecycleEntityID(EntityID id)
    {
        // index を使い回すと、古い ID が新しい別の Entity を指してしまう
        // 世代を上げておけば、IsEntityValid の世代の照合で古い ID を弾ける

        uint32_t index = EntityHandle::GetIndex(id);

        // 世代のオーバーフロー対策。UINT32_MAX まで行ったら 1 に戻す（0 は無効値として避ける）
        if (m_entityGenerations[index] == UINT32_MAX)
        {
            m_entityGenerations[index] = 1;
        }
        else
        {
            m_entityGenerations[index]++;
        }

        m_freeIndices.push_back(index);
    }



    void World::RequestDestroyEntity(EntityID entity)
    {
        PendingOp op;
        op.kind     = PendingOpKind::Destroy;
        op.entity   = entity;
        op.type     = InvalidTypeID; // Destroy にコンポーネント型はない
        op.typeSize = 0;
        op.data     = nullptr;

        m_pendingOps.Add(op);
    }



    void World::ScrutinyAndApply()
    {
        // 積んだ予約（Spawn / AddComponent / Destroy）を Chunk へ当てる
        // System の実行中に Chunk を動かすと、ループ中のイテレータが壊れる
        // そのため実行中は予約だけ積み、System が動いていないここでまとめて当てる

        ZoneScopedN("World::ScrutinyAndApply");

        // 1. 予約を当てる。返るのは破棄した EntityID
        std::vector<EntityID> deadList = m_chunkManager.ApplyPendingOperations(m_pendingOps);

        // 2. 破棄された index の世代を上げて再利用プールへ戻す
        for (EntityID id : deadList)
        {
            RecycleEntityID(id);
        }

        // 3. 予約リストを空にし、一時メモリを一括で解放する
        m_pendingOps.Clear();
        m_frameAllocator.Reset();

        // 4. 構造が変わった印。ECSContext がこれを見て ResolveTargetChunks を呼ぶ
        m_isStructureDirty = true;
    }



    const Archetype &World::GetEntityArchetype(EntityID entity) const
    {
        size_t entityIdx = 0;
        size_t chunkIdx  = m_chunkManager.SearchEntityIn(entity, &entityIdx);

        if (chunkIdx == InvalidIndex)
        {
            // 参照で返すので、寿命の続く空の Archetype を用意しておく
            static const Archetype emptyArchetype;
            return emptyArchetype;
        }

        return m_chunkManager.GetChunks()[chunkIdx]->GetArchetype();
    }



    bool World::HasComponent(EntityID entity, TypeID tid)
    {
        return GetComponentOfEntity(entity, tid) != nullptr;
    }



    void *World::GetComponentOfEntity(EntityID entity, TypeID tid)
    {
        // 世代は照合しない。照合は呼び出し元の GetComponent<T> が IsEntityValid で済ませる
        // ここを直接呼ぶと、古い ID でも中身が返る

        size_t entityIdx = 0;
        size_t chunkIdx  = m_chunkManager.SearchEntityIn(entity, &entityIdx);
        if (chunkIdx == InvalidIndex) return nullptr;
        return m_chunkManager.GetChunks()[chunkIdx]->GetComponentPtrByType(tid, entityIdx);
    }

}
