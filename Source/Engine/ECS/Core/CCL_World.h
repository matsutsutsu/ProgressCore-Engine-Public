#pragma once
// ===========================================================================
// 概要: ECS のあらゆる操作の窓口になる中央コンテナ
//
// 責務:
//   - Entity の発行・破棄・世代管理
//   - Component の Add / Remove / Patch（すべて遅延して適用する）と、その確定（ScrutinyAndApply）
//   - Resource（シングルトン的なグローバルデータ）の保持
//   - EventBus を介した System 間の通信
//
// 備考: 構造変更は必ず遅延させる。System のループ中に即時に行うと、イテレータが無効になる
//       確定はロジックの更新の頭と Prefab::SpawnPrefab の中で起き、処理対象 Chunk の一覧の作り直しは
//       System が1つも動いていない境目でだけ行う。そのため、ロジックの途中で作った Entity は次の境目まで一覧に入らない
// ===========================================================================

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Engine/ECS/Common/CCL_Types.h"
#include "CCL_ChunkManager.h"
#include "CCL_PendingOps.h"
#include "Engine/ECS/Jobs/CCL_FrameAllocator.h"
#include "Engine/ECS/Events/CCL_EventBus.h"

#include "CCL_Resource.h"
#include "CCL_EntityRef.h"
#include "CCL_ComponentHandle.h"

#include "Engine/Core/Components/DestroyTag.h"

namespace CCL::ECS
{
    // ECS の中央コンテナ。構造変更の方針はファイル先頭の備考を参照
    class World
    {
    public:
        World();
        ~World();

        World(const World &)            = delete;
        World &operator=(const World &) = delete;

        // 全 Entity を削除し World を初期状態に戻す（Resource は残す）
        void Clear();

        // ============================================================
        // フレーム更新
        // ============================================================

        // 積んだ予約（Spawn / Destroy と、コンポーネントの Add / Remove / Patch）をまとめて当てる
        // ECSContext が System を動かす前に呼ぶ。構造が変わるのはこのときだけ
        void ScrutinyAndApply();

        // ============================================================
        // コンポーネントの追加・削除（予約）
        // ============================================================

        // T を既定の構築で追加する予約を積む（当たるのは次の ScrutinyAndApply）
        // 構築と破棄の関数ポインタも運ぶので、shared_ptr などを持つ型も正しく解放される
        template <class T>
        void AddComponent(EntityID entity)
        {
            // 1. 適用時に placement new で構築するラムダ
            auto ctor = [](void *ptr) { new (ptr) T(); };

            // 2. TypeData::Create<T>() で必要な関数ポインタを一括生成
            //    （destructor / constructor / assigner / mover / name）
            TypeData td;
            td.Create<T>();

            // 3. 内部リクエストへ
            RequestAddComponent(entity,
                                TypeInfo<T>::ID(),
                                sizeof(T),
                                ctor,
                                td.destructor,
                                td.constructor,
                                td.assigner,
                                td.mover,
                                td.name);
        }

        // 引数で T を構築して追加する予約を積む。値は FrameAllocator に一時的に置く
        // 例: world.AddComponent<Position>(entity, 10.0f, 20.0f);
        template <class T, typename... Args>
        void AddComponent(EntityID entity, Args &&...args)
        {
            // 1. FrameAllocator から一時メモリを借りる（ScrutinyAndApply 後にまとめて解放）
            void *tempMem = m_frameAllocator.Alloc(sizeof(T));

            // 2. その場で T を構築
            new (tempMem) T(std::forward<Args>(args)...);

            TypeData td;
            td.Create<T>();

            // 3. 構築済みデータへのポインタを PendingOp に持たせる
            PendingOp op;
            op.kind        = PendingOpKind::AddComponent;
            op.entity      = entity;
            op.type        = TypeInfo<T>::ID();
            op.typeSize    = sizeof(T);
            op.data        = tempMem;
            op.destructor  = td.destructor;
            op.constructor = td.constructor;
            op.assigner    = td.assigner;
            op.mover       = td.mover;
            op.name        = td.name;
            op.factory     = nullptr; // 構築済みなので factory は使わない

            m_pendingOps.Add(op);
        }

        // コンポーネント削除リクエスト（型版）
        template <class T>
        void RequestRemoveComponent(EntityID entity)
        {
            RequestRemoveComponent(entity, TypeInfo<T>::ID());
        }

        // ============================================================
        // Entity の生成・破棄（予約）
        // ============================================================

        // archetype の Entity の生成を予約する（低レベル API）
        // ID はすぐ返るが、Chunk に載るのは ScrutinyAndApply の後。それまで GetComponent は nullptr
        EntityID RequestSpawnEntity(const Archetype &archetype);

        // archetype の Entity を生成し、Set を続けて書けるラッパーを返す
        // 例: world.Spawn(arch).Set(Position{0, 0}).Set(Velocity{1, 0});
        EntityRef Spawn(const Archetype &archetype);

        // 親子ごと破棄する（ゲーム側はこれを使う）。DestroyTag を付けるだけで、消すのは HierarchyCleanupSystem
        // 子を残したいときは、先に TransformUpdateSystem の DetachChild を呼ぶ
        void Destroy(EntityID id)
        {
            if (!IsEntityValid(id)) return;

            // 既にタグが付いていなければ付ける（多重付与防止）
            if (!HasComponent<Tag::DestroyTag>(id))
            {
                AddComponent<Tag::DestroyTag>(id);
            }
        }

        // 親子を見ずに、破棄の予約をすぐ積む。子は消えない。ふだんは Destroy を使う
        void RequestDestroyEntity(EntityID entity);

        // ============================================================
        // 内部コンテナへのアクセス
        // ============================================================

        // System / エディタ用
        ChunkManager       &GetChunkManager() { return m_chunkManager; }
        const ChunkManager &GetChunkManager() const { return m_chunkManager; }

        // 予約キューを直接触る必要がある場合だけ使う
        PendingOps &GetPendingOps() { return m_pendingOps; }

        // ============================================================
        // 世代管理・妥当性チェック
        // ============================================================

        // 世代を照合して、ID が今も有効かを返す。index が使い回されても、古い ID は false になる
        inline bool IsEntityValid(EntityID entity) const;

        // ============================================================
        // Resource（特定の Entity に属さない、世界に1つだけのデータ）
        // ============================================================
        //
        // 例: Grid（空間分割）、DeltaTime（時間）、Input（入力状態）など
        // ResourceIDGenerator で型 T → uint32_t を採番し、その値を配列の添字に
        // 使うので O(1) で引ける

        // T のリソースを args で構築して登録する
        template <typename T, typename... Args>
        void AddResource(Args &&...args)
        {
            const size_t id = ResourceIDGenerator::GetID<T>();
            if (id >= m_resourcesArray.size())
            {
                m_resourcesArray.resize(id + 1);
            }
            // std::forward で Args（引数）を完全転送し、
            // ResourceWrapper<T> のコンストラクタに渡す
            m_resourcesArray[id] =
                std::make_unique<ResourceWrapper<T>>(std::forward<Args>(args)...);
        }

        // 登録済みのリソース T を返す。未登録なら assert で落ちる（先に HasResource で確かめる）
        template <typename T>
        T &GetResource()
        {
            const size_t id = ResourceIDGenerator::GetID<T>();
            assert(id < m_resourcesArray.size() && m_resourcesArray[id] != nullptr && "Requested resource not found!");
            return static_cast<ResourceWrapper<T> *>(m_resourcesArray[id].get())->data;
        }

        template <typename T>
        bool HasResource() const
        {
            const size_t id = ResourceIDGenerator::GetID<T>();
            return id < m_resourcesArray.size() && m_resourcesArray[id] != nullptr;
        }

        // ============================================================
        // コンポーネントの取得
        // ============================================================

        // Entity のコンポーネント T を返す。なければ nullptr
        // 構造の変更（ScrutinyAndApply）をまたいで持つと宙に浮くので、フレームをまたぐなら GetHandle を使う
        template <class T>
        inline T *GetComponent(EntityID entity);

        template <class T>
        bool HasComponent(EntityID entity)
        {
            return GetComponent<T>(entity) != nullptr;
        }

        // TypeID 指定版
        bool HasComponent(EntityID entity, TypeID tid);

        // 構造が変わっても使えるハンドルを返す。フレームをまたいで持つならこちら
        template <typename T>
        ComponentHandle<T> GetHandle(EntityID entity)
        {
            if (HasComponent<T>(entity))
            {
                return ComponentHandle<T>(this, entity);
            }
            return ComponentHandle<T>();
        }

        // TypeID 指定の低レベル取得（エディタ・シリアライザ用）。世代チェックはしない
        void *GetComponentOfEntity(EntityID entity, TypeID tid);

        // ============================================================
        // エディタ・検査用
        // ============================================================

        // Entity の今の Archetype を返す。いなければ空。エディタが、持っているコンポーネントを並べるのに使う
        const Archetype &GetEntityArchetype(EntityID entity) const;

        // ============================================================
        // View / Query
        // ============================================================

        // Components... をすべて持つ Entity の ID を集めて返す（エディタなどの単発の処理用）
        // 毎フレームのループには、System の ForEach か ViewEach を使う
        template <class... Components>
        inline std::vector<EntityID> View();

        // View と同じ条件の Entity を、vector に貯めずに1件ずつ fn(EntityID) へ渡す
        // View と違い、空きスロットと削除予約済みの Entity は渡らない
        // fn の中で AddComponent や Destroy を呼んでも、予約になるので走査中に Chunk は動かない
        template <class... Components, class Func>
        inline void ViewEach(Func &&fn);

        // ============================================================
        // シングルトンコンポーネント
        // ============================================================

        // 持つ Entity が1体だけの型（シングルトン）を返す。見つけたら打ち切るので、毎フレーム引き直してよい
        template <typename T>
        inline T *GetSingletonComponent();

        // ============================================================
        // 非テンプレート版の予約 API（テンプレート版から呼ばれる）
        // ============================================================

        // コンポーネント追加の予約を1件積む。ctor があれば当てるときにその場で構築し、なければゼロ埋めの値を持たせる
        // assigner は型構成が同じまま値を上書きするとき、mover はコピーできない型を運ぶときに使う
        void RequestAddComponent(EntityID                           entity,
                                 TypeID                             tid,
                                 size_t                             size,
                                 const std::function<void(void *)> &ctor,
                                 const Destructor                  &dtor,
                                 TypeData::ConstructFunc            copyCtor,
                                 TypeData::AssignFunc               assigner,
                                 TypeData::MoveFunc                 mover,
                                 const char                        *name);

        // コンポーネント削除の予約を1件積む
        void RequestRemoveComponent(EntityID entity, TypeID tid);

        // ============================================================
        // コンポーネントの部分書き換え（予約）
        // ============================================================

        // 既存のコンポーネントをラムダで部分的に書き換える予約を積む
        // Prefab を生成した直後など、まだ GetComponent できないときに使う
        // 例: world.PatchComponent<TransformComponent>(entity, [pos](auto &t) { t.position = pos; });
        template <class T>
        void PatchComponent(EntityID entity, std::function<void(T &)> action)
        {
            if (!IsEntityValid(entity)) return;

            PendingOp op;
            op.kind   = PendingOpKind::PatchComponent;
            op.entity = entity;
            op.type   = TypeInfo<T>::ID();

            // 型付きの action を、ChunkManager が扱える void* 受けのラムダに包む
            op.patcher = [action](void *ptr) {
                action(*reinterpret_cast<T *>(ptr));
            };

            m_pendingOps.Add(op);
        }

        // ============================================================
        // イベント・構造変更フラグ
        // ============================================================

        EventBus &GetEventBus() { return m_eventBus; }

        // ScrutinyAndApply 後に立つ。ECSContext が処理対象 Chunk の再解決に使う
        bool IsStructureDirty() const { return m_isStructureDirty; }

        // ECSContext が再解決後に呼ぶ
        void ResetStructureDirty() { m_isStructureDirty = false; }

    private:
        // 破棄された Entity の index の世代を上げ、再利用プールへ戻す
        void RecycleEntityID(EntityID id);

    private:
        FrameAllocator m_frameAllocator; // 予約データの一時置き場（ScrutinyAndApply でリセット）
        ChunkManager   m_chunkManager;
        PendingOps     m_pendingOps; // 構造変更の予約キュー
        EventBus       m_eventBus;

        std::vector<uint32_t> m_entityGenerations;   // index ごとの現在の世代
        std::vector<uint32_t> m_freeIndices;         // 再利用待ちの index
        uint32_t              m_nextUnusedIndex = 1; // まだ一度も使っていない index の先頭（0 は予約）

        bool m_isStructureDirty = true; // true なら処理対象 Chunk の再解決が必要

        // 型消去したリソース群（添字は ResourceIDGenerator の ID）
        std::vector<std::unique_ptr<ResourceBase>> m_resourcesArray;
    };

    // ============================================================
    // インライン実装
    // ============================================================
    //
    // ここから下は World の完全定義が必要な実装
    // EntityRef / ComponentHandle のテンプレート実装も、World を完全に知った
    // 後でないと書けないので、ここに集めている

    inline bool World::IsEntityValid(EntityID entity) const
    {
        const uint32_t index = EntityHandle::GetIndex(entity);
        const uint32_t gen   = EntityHandle::GetGeneration(entity);

        // 範囲外
        if (index >= m_entityGenerations.size()) return false;

        // 世代不一致（削除・再利用済みの古い ID）
        if (m_entityGenerations[index] != gen) return false;

        return true;
    }

    template <class T>
    inline T *World::GetComponent(EntityID entity)
    {
        if (!IsEntityValid(entity)) return nullptr;
        return static_cast<T *>(GetComponentOfEntity(entity, TypeInfo<T>::ID()));
    }

    template <class... Components>
    inline std::vector<EntityID> World::View()
    {
        std::vector<EntityID> entities;

        Archetype filter = ArchetypeHelper::Generate<Components...>();

        auto &chunks = m_chunkManager.GetChunks();
        for (const auto &upChunk : chunks)
        {
            if (!upChunk) continue;
            Chunk *chunk = upChunk.get();

            // Chunk が filter を包含していれば、その全 Entity を採用する
            if (ArchetypeHelper::LhasR(chunk->GetArchetype(), filter))
            {
                const size_t    count = chunk->GetEntityCount();
                const EntityID *ids   = chunk->GetEntityIDs();
                entities.insert(entities.end(), ids, ids + count);
            }
        }
        return entities;
    }

    template <class... Components, class Func>
    inline void World::ViewEach(Func &&fn)
    {
        const Archetype filter = ArchetypeHelper::Generate<Components...>();

        for (const auto &upChunk : m_chunkManager.GetChunks())
        {
            if (!upChunk) continue;
            Chunk *chunk = upChunk.get();
            if (!ArchetypeHelper::LhasR(chunk->GetArchetype(), filter)) continue;

            const size_t    count        = chunk->GetEntityCount();
            const EntityID *ids          = chunk->GetEntityIDs();
            const bool      hasDestroyed = chunk->HasDestroyedEntities();

            for (size_t i = 0; i < count; ++i)
            {
                // 無効化されたスロット（穴は CompactHoles まで詰められない）
                if (ids[i] == InvalidEntityID) continue;
                // 削除予約済み。hasDestroyed が false の間は判定自体を飛ばす
                if (hasDestroyed && chunk->IsEntityDestroyed(i)) continue;

                fn(ids[i]);
            }
        }
    }

    template <typename T>
    inline T *World::GetSingletonComponent()
    {
        const Archetype filter = ArchetypeHelper::Generate<T>();
        const TypeID    tid    = TypeInfo<T>::ID();

        // 1件見つけた時点で返す。vector に貯めてから先頭を取り直す必要はない
        for (const auto &upChunk : m_chunkManager.GetChunks())
        {
            if (!upChunk) continue;
            Chunk *chunk = upChunk.get();
            if (!ArchetypeHelper::LhasR(chunk->GetArchetype(), filter)) continue;

            const size_t    count        = chunk->GetEntityCount();
            const EntityID *ids          = chunk->GetEntityIDs();
            const bool      hasDestroyed = chunk->HasDestroyedEntities();

            for (size_t i = 0; i < count; ++i)
            {
                if (ids[i] == InvalidEntityID) continue;
                if (hasDestroyed && chunk->IsEntityDestroyed(i)) continue;

                return static_cast<T *>(chunk->GetComponentPtrByType(tid, i));
            }
        }
        return nullptr;
    }

    // EntityRef のテンプレート実装

    template <typename T>
    inline EntityRef &EntityRef::Set(T &&component)
    {
        world->AddComponent<std::decay_t<T>>(id, std::forward<T>(component));
        return *this;
    }

    template <typename T, typename Func>
    inline EntityRef &EntityRef::Patch(Func &&func)
    {
        world->PatchComponent<T>(id, std::forward<Func>(func));
        return *this;
    }

    // ComponentHandle のテンプレート実装

    template <typename T>
    inline T *ComponentHandle<T>::Get() const
    {
        if (!m_world) return nullptr;
        return m_world->GetComponent<T>(m_id);
    }

    template <typename T>
    inline bool ComponentHandle<T>::IsValid() const
    {
        return Get() != nullptr;
    }

    inline EntityRef World::Spawn(const Archetype &archetype)
    {
        EntityID id = RequestSpawnEntity(archetype);
        return EntityRef{this, id};
    }

} // namespace CCL::ECS
