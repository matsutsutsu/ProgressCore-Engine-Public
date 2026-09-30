#include "CCL_World.h"
#include "../System/CCL_SystemManager.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include <algorithm>
#include <cassert>
#include <cstring>

#include "tracy/Tracy.hpp"

namespace CCL::ECS
{
    // index 0 を使わないのは、0 を「無効・未設定」の意味で使いたいため。
    // 1 から配ることで、ゼロ初期化されただけの EntityID を有効な Entity と誤認しない。
    // Tools/ECSPlayground の実験2で最初の ID が index:1 になるのはこれが理由。
    World::World()
    {
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

        // 2. イベントの購読も全て白紙に戻す
        m_eventBus.Clear();

        // 3. ID 管理情報を初期状態（index は 1 から）に戻す
        m_entityGenerations.clear();
        m_freeIndices.clear();
        m_nextUnusedIndex = 1;

        // m_resourcesArray（シングルトン）は意図的に残す。
        // ここは Entity のリセットが目的であり、リソースは Initialize() で
        // 一度だけ登録する設計のため、消すと再登録の手当てが必要になる。
    }

    // なぜこんなに関数ポインタを持ち回るのか:
    //   Chunk は void* の生メモリを扱うので、型情報が消えている。
    //   「この番地を正しい型として破棄・移動・代入する」手段を
    //   データと一緒に運ぶ必要がある（型消去）。
    //
    // 先頭の IsEntityValid で弾いているのは、既に死んだ ID への操作を無視するため。
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
        if (!IsEntityValid(entity)) return;

        PendingOp op;
        op.kind       = PendingOpKind::AddComponent;
        op.entity     = entity;
        op.type       = tid;
        op.typeSize   = size;
        op.destructor = dtor;
        // ChunkManager::CalculateArchetypeDiff で TypeData を組み立てるときに使う。
        // これらが nullptr だと memcpy へのフォールバックが起きる
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
            // ctor が無い場合は、ゼロ埋めした一時データをコピー元として持たせる
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

    // 「予約番号」を発行して返すだけで、Chunk には一切触れない。
    // Tools/ECSPlayground の実験2で「Spawn 直後は GetComponent が nullptr」なのはこのため。
    EntityID World::RequestSpawnEntity(const Archetype &archetype)
    {
        uint32_t index;
        uint32_t generation;

        // Phase 1: 世代付き ID の発行
        //   m_freeIndices に空きがあれば再利用（世代は RecycleEntityID で上げ済み）。
        //   無ければ m_nextUnusedIndex++ で新規発行し、世代は 1 から始める。
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

        // Phase 2: PendingOp への登録
        //   Archetype は【値でコピー】して持たせる。
        //   FrameAllocator 上に置いてポインタで渡すと、FrameAllocator::Reset() の
        //   タイミング次第で破棄済みメモリを指してクラッシュする。
        //   値で持てば PendingOp が生きている限り安全。
        PendingOp op;
        op.kind           = PendingOpKind::Spawn;
        op.entity         = id;
        op.spawnArchetype = archetype;
        op.data           = nullptr;

        m_pendingOps.Add(op);

        // ID は即座に返す（呼び出し側はすぐ AddComponent 等に使える）
        return id;
    }

    // ダングリング ID 対策の本体:
    //   index を再利用すると、古い EntityID が新しい別の Entity を指してしまう。
    //   世代を上げておけば、IsEntityValid() の世代照合で古い ID を確実に弾ける。
    //
    //   Tools/ECSPlayground の実験3の出力がまさにこれ:
    //     破棄した古い ID = index:3 / 世代:1  → IsEntityValid = false
    //     新しい ID       = index:3 / 世代:2  → IsEntityValid = true
    void World::RecycleEntityID(EntityID id)
    {
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
        op.type     = InvalidTypeID; // Destroy にコンポーネント型は無い
        op.typeSize = 0;
        op.data     = nullptr;

        m_pendingOps.Add(op);
    }

    // フレームの境界線そのもの。この関数の前後で世界が変わる:
    //   入る前  : Spawn / AddComponent / Destroy は全て「予約」でしかない。
    //             新しく Spawn した Entity の GetComponent は nullptr。
    //   抜けた後: 実体が Chunk に載っていて、GetComponent がアドレスを返す。
    //
    // System 実行中に構造を変えてはいけない理由:
    //   System 実行中に entity.AddComponent<Velocity>() で即座に Chunk 移動が起きると、
    //   今まさにループ中の Chunk 構造が変わり、イテレータが無効になって
    //   クラッシュ・メモリ破壊になる。そこで System 実行中は予約だけを積み、
    //   System が1つも動いていないここで、まとめて整合性を保ちながら適用する。
    //
    // Tools/ECSPlayground の実験2で追っているのがこの関数。
    // F11 で ApplyPendingOperations の中へ潜ると本体が見える。
    void World::ScrutinyAndApply()
    {
        ZoneScopedN("World::ScrutinyAndApply");

        // 1. 予約を ChunkManager に渡して、実際のメモリ移動・生成・破棄を行う。
        //    実際に破棄された EntityID のリストが返る
        std::vector<EntityID> deadList = m_chunkManager.ApplyPendingOperations(m_pendingOps);

        // 2. 破棄された index の世代を上げて再利用プールへ戻す
        for (EntityID id : deadList)
        {
            RecycleEntityID(id);
        }

        // 3. 予約リストを空にし、一時メモリを一括で解放する。
        //    AddComponent の一時データ（op.data）は Clear でデストラクタを呼んでから、
        //    その領域を Reset で再利用できる状態に戻す
        m_pendingOps.Clear();
        m_frameAllocator.Reset();

        // 4. 「構造が変わった」印。ECSContext がこれを見て ResolveTargetChunks を呼ぶ
        //    （変化が無ければ ResolveTargetChunks 側のキャッシュ判定ですぐ抜ける）
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

    // 住所録で場所を O(1) で引き、その Chunk から番地を計算して返す。
    //
    // この関数は世代チェックをしない。世代照合は呼び出し元の GetComponent<T>
    // （World.h のテンプレート版）が IsEntityValid() で先に済ませる役割分担。
    // ここを直接呼ぶと、古い世代の ID でも中身が返ってきてしまう。
    void *World::GetComponentOfEntity(EntityID entity, TypeID tid)
    {
        size_t entityIdx = 0;
        size_t chunkIdx  = m_chunkManager.SearchEntityIn(entity, &entityIdx);
        if (chunkIdx == InvalidIndex) return nullptr;
        return m_chunkManager.GetChunks()[chunkIdx]->GetComponentPtrByType(tid, entityIdx);
    }

} // namespace CCL::ECS
