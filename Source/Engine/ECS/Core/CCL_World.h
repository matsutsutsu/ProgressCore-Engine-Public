#pragma once
// ===========================================================================
// 概要: ECS のあらゆる操作の窓口となる中央コンテナ World。
//
// 責務:
//   - Entity の発行・破棄・世代管理
//   - Component の Add / Remove / Patch（全て遅延適用）
//   - 構造変更（PendingOps）の確定（ScrutinyAndApply）
//   - Resource（シングルトン的グローバルデータ）の保持
//   - EventBus を介した System 間通信
//
// 備考: 構造変更（Add/Remove/Spawn/Destroy）は必ず遅延実行する。
//       System のループ中に即時実行するとイテレータが無効になりクラッシュするため、
//       PendingOps に積んで ScrutinyAndApply() で一括適用する。
//
//       確定と、System の処理対象 Chunk の一覧の作り直しは、別のタイミングで起きる。
//         確定（ScrutinyAndApply）   : ロジックの更新の頭に加え、Prefab::SpawnPrefab の中でもその場で起きる。
//                                      プレハブを1つ生成すると、そのとき積まれている予約が World 全体ぶん確定する。
//         一覧の作り直し             : System が1つも動いていない境目（ロジックの更新の頭・描画の更新の頭）でだけ行う。
//                                      並列に走る System が一覧を回っている最中に作り直すと壊れるため（CCL_ECSContext.h）。
//       そのため、ロジックの途中で生成した Entity は、次の境目まで後ろの System の一覧に入らない。
//
//       関連: EntityRef（生成直後のメソッドチェーン）/ ComponentHandle（安全な参照）/
//             ResourceWrapper（型消去したリソース格納）
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

#include "CCL_Resource.h"        // ResourceIDGenerator / ResourceBase / ResourceWrapper
#include "CCL_EntityRef.h"       // EntityRef
#include "CCL_ComponentHandle.h" // ComponentHandle<T>

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

        /**
         * @brief 溜まった PendingOps を確定し、Chunk 構造を整合させる。
         *
         * - Entity の Spawn / Destroy
         * - Component の Add / Remove / Patch
         *
         * を一括処理し、必要に応じて Chunk 間遷移と CompactHoles を行う。
         * ECSContext から「System 実行の前」に呼ばれる。この時だけ構造が変わる。
         */
        void ScrutinyAndApply();

        // ============================================================
        // コンポーネントの追加・削除（予約）
        // ============================================================

        /**
         * @brief コンポーネント追加リクエスト（デフォルト構築版）。
         *
         * 型 T のデフォルトコンストラクタで構築したコンポーネントを、
         * 次回の ScrutinyAndApply で追加する。構築・破棄の関数ポインタも
         * 一緒に運ぶので、shared_ptr 等を持つ型でも正しく解放される。
         */
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

        /**
         * @brief コンポーネント追加リクエスト（引数転送版）。
         *
         * 与えられた引数で T をその場で構築して FrameAllocator 上に一時保持し、
         * PendingOp に積む。引数が0個の呼び出しは、より特殊化された
         * デフォルト構築版の方が選ばれる。
         *
         * @code
         *   world.AddComponent<Position>(entity, 10.0f, 20.0f); // 引数渡し
         *   world.AddComponent<Velocity>(entity, someInstance); // インスタンス渡し
         * @endcode
         */
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

        /**
         * @brief 指定 Archetype のエンティティ生成を予約する（低レベル API）。
         *
         * ID は即座に返るので呼び出し直後から AddComponent 等に使えるが、
         * Chunk への配置は ScrutinyAndApply まで遅延される
         * （それまで GetComponent は nullptr を返す）。
         */
        EntityID RequestSpawnEntity(const Archetype &archetype);

        /**
         * @brief Archetype から生成し、メソッドチェーン用ラッパーを返す（推奨 API）。
         *
         * @code
         *   world.Spawn(arch)
         *        .Set(Position{0, 0})
         *        .Set(Velocity{1, 0});
         * @endcode
         *
         * @note 実装は本ヘッダ末尾。
         */
        EntityRef Spawn(const Archetype &archetype);

        /**
         * @brief 親子関係対応の安全な破棄（推奨：ゲーム側用）。
         *
         * この Entity に DestroyTag を付けるだけで、実際の破棄は
         * HierarchyCleanUpSystem が DestroyTag を見て行う。
         * 「親が消えたら子も消える」などの階層処理を1か所に集めるため。
         *
         * @note 親が消えても子を残したい場合は、Destroy の直前に
         *       TransformUpdateSystem の DetachChild を呼ぶこと。
         */
        void Destroy(EntityID id)
        {
            if (!IsEntityValid(id)) return;

            // 既にタグが付いていなければ付ける（多重付与防止）
            if (!HasComponent<Tag::DestroyTag>(id))
            {
                AddComponent<Tag::DestroyTag>(id);
            }
        }

        /**
         * @brief 親子関係を完全に無視する強制破棄（システム内部用）。
         *
         * Destroy() がタグベースの「ソフト削除」なのに対し、こちらは
         * 直接 Destroy の PendingOp を積む。HierarchyCleanUpSystem 自身や
         * フレームワーク内部で使う想定。ゲームロジックからは Destroy() を使うこと。
         */
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

        /**
         * @brief EntityID が現時点で有効かを世代カウンタで判定する。
         *
         * 削除されたインデックスが再利用されたとき、古い EntityID が新しい
         * 別の Entity を誤って指してしまう問題を防ぐ。
         */
        inline bool IsEntityValid(EntityID entity) const;

        // ============================================================
        // Resource（特定の Entity に属さない、世界に1つだけのデータ）
        // ============================================================
        //
        // 例: Grid（空間分割）、DeltaTime（時間）、Input（入力状態）など。
        // ResourceIDGenerator で型 T → uint32_t を採番し、その値を配列の添字に
        // 使うので O(1) で引ける。

        /**
         * @brief 型 T のリソースを生成して World に登録する。
         * @tparam T    リソース型
         * @tparam Args T のコンストラクタに渡す引数
         */
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

        /**
         * @brief 登録済みリソース T への参照を取得する（O(1)）。
         *
         * @note 未登録の場合は assert で落ちる。事前に HasResource で確認するか、
         *       Initialize で必ず AddResource しておくこと。
         */
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

        /**
         * @brief 指定 Entity の Component T を取得する。
         * @return 見つかれば T*、無ければ nullptr
         *
         * @note 返るのは生ポインタ。構造変更（ScrutinyAndApply）を跨いで保持すると
         *       ダングリングするので、フレームを跨ぐなら GetHandle を使うこと。
         */
        template <class T>
        inline T *GetComponent(EntityID entity);

        template <class T>
        bool HasComponent(EntityID entity)
        {
            return GetComponent<T>(entity) != nullptr;
        }

        // TypeID 指定版
        bool HasComponent(EntityID entity, TypeID tid);

        /**
         * @brief 構造変更に強い ComponentHandle を取得する（推奨）。
         *
         * 生ポインタを長く保持するとダングリングするので、フレームを跨いだ
         * 参照には必ずこちらを使うこと。
         */
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

        /**
         * @brief 指定 Entity の現在の Archetype を取得する。
         *
         * エディタが Chunk の内部構造を知らずに「この Entity は何を持っているか」を
         * 一覧化するために使う。存在しない Entity なら空の Archetype を返す。
         */
        const Archetype &GetEntityArchetype(EntityID entity) const;

        // ============================================================
        // View / Query
        // ============================================================

        /**
         * @brief 指定の Components... を全て持つ Entity の ID 一覧を取得する。
         *
         * @code
         *   auto enemies = world->View<Position, Enemy>();
         * @endcode
         *
         * @note 高頻度ループでは System の ForEach を使う方が速い。
         *       本関数はエディタ等の単発処理向け。
         */
        template <class... Components>
        inline std::vector<EntityID> View();

        /**
         * @brief View と同じ条件の Entity を、確保せずに1件ずつ fn(EntityID) へ渡す。
         *
         * @details View は結果を vector に貯めるため、呼ぶたびにヒープ確保が起きる。
         *          毎フレーム走る処理や、ドローンのように大量に該当するクエリでは
         *          その確保自体が無視できないので、貯めずに流すこちらを使う。
         *
         * @code
         *   world->ViewEach<DroneComponent, TransformComponent>([&](EntityID id) { ... });
         * @endcode
         *
         * @note View と違い、空きスロットと削除予約済みの Entity は渡らない。
         * @note コールバック内で AddComponent/DestroyEntity を呼んでよい（構造変更は
         *       ScrutinyAndApply まで遅延されるため、走査中に Chunk は動かない）。
         *       ただし即時に効く操作を足す場合はこの前提が崩れる点に注意。
         * @note 高頻度ループでは System の ForEach の方がさらに速い。
         *       本関数は「自分のアーキタイプ外を覗きたい」ときの手段。
         */
        template <class... Components, class Func>
        inline void ViewEach(Func &&fn);

        // ============================================================
        // シングルトンコンポーネント
        // ============================================================

        /**
         * @brief シングルトンコンポーネント（持つ Entity が1体だけの型）を取得する。
         *
         * @note 見つけた時点で打ち切るため、ヒープ確保も全 Chunk 走査も起きない。
         *       毎フレーム引き直してよい（＝ポインタを持ち回らなくてよい）。
         *       生ポインタをフレームを跨いで保持すると Chunk 移動/穴詰めでダングリングするので、
         *       保持したい場合は GetHandle() を使うこと。
         */
        template <typename T>
        inline T *GetSingletonComponent();

        // ============================================================
        // 非テンプレート版の予約 API（テンプレート版から呼ばれる）
        // ============================================================

        /**
         * @brief コンポーネント追加の予約を1件積む。ここでは何も追加されない。
         *
         * @param ctor     構築関数。あれば適用時にその場で構築する（factory）。
         *                 無ければゼロ埋めした一時データを持たせる
         * @param dtor     デストラクタ
         * @param copyCtor コピー構築
         * @param assigner 代入演算子（同じ型構成のまま値だけ上書きするときに使う）
         * @param mover    ムーブ構築（コピー禁止型でも運べるようにするため）
         * @param name     デバッグ表示用の型名
         */
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

        /**
         * @brief 既存コンポーネントの中身を、ラムダで部分書き換えする予約を積む。
         *
         * Prefab 生成直後で GetComponent がまだできないタイミングでも、
         * 「特定のフィールドだけ差し替えたい」用途に使える。
         *
         * @code
         *   world.PatchComponent<TransformComponent>(entity, [pos](auto& t){
         *       t.position = pos;   // position だけ変更、scale 等は維持
         *   });
         * @endcode
         */
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
    // ここから下は World の完全定義が必要な実装。
    // EntityRef / ComponentHandle のテンプレート実装も、World を完全に知った
    // 後でないと書けないので、ここに集めている。

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

        // 1件見つけた時点で返す。vector に貯めてから先頭を取り直す必要はない。
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
