#pragma once
// ===========================================================================
// CCL_World.h
// -----------
// ECS のあらゆる操作の「窓口」となる World クラスを定義します。
//
// World は次の責務を持ちます:
//   - Entity の発行・破棄・世代管理
//   - Component の Add / Remove / Patch（全て遅延適用）
//   - 構造変更 (PendingOps) の確定 (ScrutinyAndApply)
//   - Resource（シングルトン的グローバルデータ）の保持
//   - EventBus を介したシステム間通信
//
// 関連クラス（責務分割済み）:
//   - EntityRef        : エンティティ操作用のメソッドチェーンラッパー
//   - ComponentHandle  : 構造変更に強い安全なコンポーネントアクセス
//   - ResourceWrapper  : 型安全な型消去リソース格納
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

// ---- 責務分割済みの構成要素を取り込む ----
#include "CCL_Resource.h"        // ResourceIDGenerator / ResourceBase / ResourceWrapper
#include "CCL_EntityRef.h"       // EntityRef
#include "CCL_ComponentHandle.h" // ComponentHandle<T>

#include "Engine/Core/Components/DestroyTag.h"


namespace CCL::ECS
{
    using namespace CCL::ECS;


    /**
     * @brief エンティティの「現在地」を表す住所構造体。
     *
     * EntityID から「どの Chunk の何番目に居るか」を直接示します。
     * （ChunkManager 内部の EntityLocation は index ベースですが、
     *   こちらは Chunk* ベース。用途に応じて使い分けます）
     */
    struct EntityLocation
    {
        Chunk* chunk = nullptr; // どの Chunk か
        size_t index = 0;       // その Chunk 内のインデックス
    };


    // ======================================================================
    // World
    // ======================================================================

    /**
     * @brief ECS の中央コンテナ。あらゆる Entity / Component 操作の窓口。
     *
     * 設計上の重要原則:
     *   - 構造変更（Add/Remove/Spawn/Destroy）は **必ず遅延実行**
     *     System ループ中に即時実行するとイテレータが無効化してクラッシュ
     *     するため、PendingOps に積み上げて ScrutinyAndApply() で一括適用
     *   - シングルトン的でコピー禁止
     */
    class World
    {
    public:
        World();
        ~World();

        World(const World&)            = delete;
        World& operator=(const World&) = delete;

        // 全エンティティを削除し World を初期状態に戻す
        void Clear();


        // --------------------------------------------------------
        // Frame API (更新フロー)
        // --------------------------------------------------------

        /**
         * @brief 溜まった PendingOps を確定し、Chunk 構造を整合させる。
         *
         * - Entity の Spawn / Destroy
         * - Component の Add / Remove / Patch
         *
         * を一括処理し、必要に応じて Chunk 間遷移と CompactHoles を行います。
         * 通常は ECSContext から「System 実行の前」に呼ばれます。
         */
        void ScrutinyAndApply();


        // --------------------------------------------------------
        // Component Operations (Template API)
        // --------------------------------------------------------

        /**
         * @brief コンポーネント追加リクエスト（デフォルト構築版）。
         *
         * 型 T のデフォルトコンストラクタで構築されたコンポーネントを、
         * 次回 ScrutinyAndApply 時に追加します。コンストラクタ・デストラクタ
         * の関数ポインタも合わせて登録するため、shared_ptr 等を持つ型でも
         * 正しく解放されます。
         */
        template <class T>
        void AddComponent(EntityID entity)
        {
            // 1. プレースメント new を行うラムダ（コンストラクタ呼出器）
            auto ctor = [](void* ptr) { new (ptr) T(); };

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
         * @brief コンポーネント追加リクエスト（万能版：引数転送）。
         *
         * 与えられた引数で T を構築し、FrameAllocator 上に一時保持してから
         * PendingOp に積みます。引数 0 個でも N 個でも、すべてこの経路で安全に
         * 処理されます。
         *
         * @code
         *   world.AddComponent<Position>(entity, 10.0f, 20.0f); // 引数渡し
         *   world.AddComponent<Velocity>(entity, someInstance); // インスタンス渡し
         * @endcode
         */
        template <class T, typename... Args>
        void AddComponent(EntityID entity, Args&&... args)
        {
            // 1. FrameAllocator から一時メモリを借りる（ヒープより遥かに高速）
            void* tempMem = m_frameAllocator.Alloc(sizeof(T));

            // 2. その場で T を構築
            new (tempMem) T(std::forward<Args>(args)...);

            TypeData td;
            td.Create<T>();

            // 3. ポインタを PendingOp に直接渡す
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
            op.factory     = nullptr; // std::function の経路は使わない（高速化）

            m_pendingOps.Add(op);
        }

        // コンポーネント削除リクエスト（型版）
        template <class T>
        void RequestRemoveComponent(EntityID entity)
        {
            RequestRemoveComponent(entity, TypeInfo<T>::ID());
        }


        // --------------------------------------------------------
        // Entity Operations
        // --------------------------------------------------------

        /**
         * @brief 指定 Archetype のエンティティ生成を予約する（低レベル API）。
         *
         * ID は即座に返るので呼び出し直後から AddComponent 等で参照できますが、
         * 実体 Chunk への配置は ScrutinyAndApply まで遅延されます。
         */
        EntityID RequestSpawnEntity(const Archetype& archetype);

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
        EntityRef Spawn(const Archetype& archetype);


        /**
         * @brief 親子関係対応の安全な破棄（推奨：ゲーム側用）。
         *
         * このエンティティに DestroyTag を付けるだけで、実際の破棄処理は
         * HierarchyCleanUpSystem が DestroyTag を見て行います。
         * これにより、親が死んだら子も一緒に消えるなどの階層的な処理が
         * 統一して扱われます。
         *
         * @note 親が死んでも子を残したい場合は、Destroy 直前に
         *       TransformUpdateSystem の DetachChild を呼んでください。
         */
        void Destroy(EntityID id)
        {
            if (!IsEntityValid(id)) return;

            // 既にタグが付いていなければ付ける（多重付与防止）
            if (!HasComponent<Tag::DestroyTag>(id)) {
                AddComponent<Tag::DestroyTag>(id);
            }
        }

        /**
         * @brief 親子関係を完全に無視する強制破棄（システム内部用）。
         *
         * Destroy() がタグベースの「ソフト削除」なのに対し、こちらは
         * 直接 PendingOps::Destroy を積みます。HierarchyCleanUpSystem 自身や
         * フレームワーク内部で使うことが想定されており、ゲームロジックからは
         * 通常 Destroy() を使ってください。
         */
        void RequestDestroyEntity(EntityID entity);


        // --------------------------------------------------------
        // Accessors（内部コンテナへの参照取得）
        // --------------------------------------------------------

        // ChunkManager への参照を返す（System / Editor 用）
        ChunkManager&       GetChunkManager()       { return m_chunkManager; }
        const ChunkManager& GetChunkManager() const { return m_chunkManager; }

        // PendingOps への参照を返す（高度な利用向け）
        PendingOps& GetPendingOps() { return m_pendingOps; }


        // --------------------------------------------------------
        // 世代管理・妥当性チェック
        // --------------------------------------------------------

        /**
         * @brief EntityID が現時点で有効かを世代カウンタで判定する。
         *
         * 削除されたインデックスが再利用されたとき、古い EntityID が新しい
         * 別エンティティを誤って指してしまう問題を防ぐためのチェック。
         */
        inline bool IsEntityValid(EntityID entity) const;

        // IsEntityValid のエイリアス（API 名の好み吸収用）
        inline bool ValidateEntityGeneration(EntityID entity) const
        {
            return IsEntityValid(entity);
        }


        // --------------------------------------------------------
        // Resource Management (Singleton Component, O(1) Access)
        // --------------------------------------------------------
        //
        // 特定の Entity に属さない「世界に1つだけのデータ」を保持する仕組み。
        // 例: Grid（空間分割）、DeltaTime（時間）、Input（入力状態）など。
        //
        // ResourceIDGenerator により型 T → uint32_t を採番し、その値を配列
        // インデックスとして使うため、unordered_map より高速にアクセスできます。
        //

        /**
         * @brief 型 T のリソースを生成して World に登録する。
         * @tparam T    リソース型
         * @tparam Args T のコンストラクタに渡す引数
         */
        template <typename T, typename... Args>
        void AddResource(Args&&... args)
        {
            const size_t id = ResourceIDGenerator::GetID<T>();
            if (id >= m_resourcesArray.size()) {
                m_resourcesArray.resize(id + 1);
            }
            m_resourcesArray[id] =
                std::make_unique<ResourceWrapper<T>>(std::forward<Args>(args)...);
        }

        /**
         * @brief 登録済みリソース T への参照を取得する（O(1)）。
         *
         * @note 未登録の場合は assert で落ちます。事前に HasResource で確認するか、
         *       Initialize 時に必ず AddResource しておく運用にしてください。
         */
        template <typename T>
        T& GetResource()
        {
            // 型 T → ID → 配列インデックス という O(1) アクセス
            const size_t id = ResourceIDGenerator::GetID<T>();
            assert(id < m_resourcesArray.size() && m_resourcesArray[id] != nullptr
                   && "Requested resource not found!");
            return static_cast<ResourceWrapper<T>*>(m_resourcesArray[id].get())->data;
        }

        // リソース T が登録済みかを判定する
        template <typename T>
        bool HasResource() const
        {
            const size_t id = ResourceIDGenerator::GetID<T>();
            return id < m_resourcesArray.size() && m_resourcesArray[id] != nullptr;
        }


        // --------------------------------------------------------
        // Component Access
        // --------------------------------------------------------

        /**
         * @brief 指定 Entity の Component T を取得する（高速パス）。
         * @return 見つかれば T*、無ければ nullptr
         */
        template <class T>
        inline T* GetComponent(EntityID entity);

        // 指定 Entity が Component T を持っているかを判定する
        template <class T>
        bool HasComponent(EntityID entity)
        {
            return GetComponent<T>(entity) != nullptr;
        }

        // 動的型 (TypeID) 版の HasComponent
        bool HasComponent(EntityID entity, TypeID tid);


        /**
         * @brief 構造変更に強い ComponentHandle を取得する（推奨）。
         *
         * 生ポインタを長く保持するとダングリングするので、フレームを跨いだ
         * 参照には必ずこちらを使ってください。
         */
        template <typename T>
        ComponentHandle<T> GetHandle(EntityID entity)
        {
            if (HasComponent<T>(entity)) {
                return ComponentHandle<T>(this, entity);
            }
            return ComponentHandle<T>();
        }

        // TypeID 指定の低レベル取得（エディタ・シリアライザ用）
        void* GetComponentOfEntity(EntityID entity, TypeID tid);


        // --------------------------------------------------------
        // Editor / Inspection API
        // --------------------------------------------------------

        /**
         * @brief 指定 Entity の現在の Archetype を取得する。
         *
         * エディタが Chunk 内部構造を知らずに「このエンティティは何を持って
         * いるか」を一覧化するために使用します。
         */
        const Archetype& GetEntityArchetype(EntityID entity) const;


        // --------------------------------------------------------
        // View / Query
        // --------------------------------------------------------

        /**
         * @brief 指定の Components... を全て持つエンティティ ID 一覧を取得。
         *
         * @code
         *   auto enemies = world->View<Position, Enemy>();
         * @endcode
         *
         * @note 高頻度ループでは System の自動 ChunkMatch を使う方が高速です。
         *       本関数はエディタ等の単発処理向け。
         */
        template <class... Components>
        inline std::vector<EntityID> View();

        /**
         * @brief View と同じ条件のエンティティを、確保せずに1件ずつ fn(EntityID) へ渡す。
         *
         * @details View は結果を vector に貯めるため、呼ぶたびにヒープ確保が起きる。
         *          毎フレーム走る処理や、ドローンのように大量に該当するクエリでは
         *          その確保自体が無視できないので、貯めずに流すこちらを使う。
         *
         * @code
         *   world->ViewEach<DroneComponent, TransformComponent>([&](EntityID id) { ... });
         * @endcode
         *
         * @note View と違い、空席と削除予約済みのエンティティは渡らない。
         * @note コールバック内で AddComponent/DestroyEntity を呼んでよい（構造変更は
         *       ScrutinyAndApply まで遅延されるため、走査中にチャンクは動かない）。
         *       ただし即時に効く操作を足す場合はこの前提が崩れる点に注意。
         * @note 高頻度ループでは System の自動 ChunkMatch(ForEach) の方がさらに速い。
         *       本関数は「自分のアーキタイプ外を覗きたい」ときの手段。
         */
        template <class... Components, class Func>
        inline void ViewEach(Func&& fn);


		// --------------------------------------------------------
		// Singleton Component Access
		// --------------------------------------------------------

        /**
         * @brief シングルトンコンポーネントを取得する。
         *
         * シングルトンコンポーネントはエンティティが1つだけ存在することを前提としています。
         *
         * @note 見つけた時点で打ち切るため、ヒープ確保も全チャンク走査も起きない。
         *       毎フレーム引き直してよい（＝ポインタを持ち回らなくてよい）。
         *       生ポインタを跨いで保持するとチャンク移動/圧縮でダングリングするので、
         *       保持したい場合は GetHandle() を使うこと。
         */
        template <typename T>
        inline T* GetSingletonComponent();


        // --------------------------------------------------------
        // 内部実装用（テンプレートでない方の AddComponent ルート）
        // --------------------------------------------------------

        /**
         * @brief 非テンプレート版 AddComponent。テンプレ版から呼ばれて
         *        実際の PendingOp を組み立てる。
         */
        void RequestAddComponent(EntityID                          entity,
                                  TypeID                            tid,
                                  size_t                            size,
                                  const std::function<void(void*)>& ctor,
                                  const Destructor&                 dtor,
                                  TypeData::ConstructFunc           copyCtor,
                                  TypeData::AssignFunc              assigner,
                                  TypeData::MoveFunc                mover,
                                  const char*                       name);

        // 非テンプレート版 RequestRemoveComponent
        void RequestRemoveComponent(EntityID entity, TypeID tid);


        // --------------------------------------------------------
        // Component Patching (Prefab 生成後の部分修正用)
        // --------------------------------------------------------

        /**
         * @brief 既に存在するコンポーネントの中身を、ラムダで部分書き換えする。
         *
         * Prefab 生成直後で GetComponent がまだできないタイミングでも、
         * 「特定フィールドだけ差し替えたい」用途に対応します。
         *
         * @code
         *   world.PatchComponent<TransformComponent>(entity, [pos](auto& t){
         *       t.position = pos;   // position だけ変更、scale 等は維持
         *   });
         * @endcode
         */
        template <class T>
        void PatchComponent(EntityID entity, std::function<void(T&)> action)
        {
            if (!IsEntityValid(entity)) return;

            PendingOp op;
            op.kind   = PendingOpKind::PatchComponent;
            op.entity = entity;
            op.type   = TypeInfo<T>::ID();

            // 型安全な action を、void* インタフェースのラムダにラップ
            op.patcher = [action](void* ptr) {
                action(*reinterpret_cast<T*>(ptr));
            };

            m_pendingOps.Add(op);
        }


        // --------------------------------------------------------
        // Event / Dirty フラグ
        // --------------------------------------------------------

        // 「ECS 外との橋渡し」用の郵便局（イベントバス）
        EventBus& GetEventBus() { return m_eventBus; }

        // 構造変更が今フレーム発生したかを返す（最適化フラグ）
        bool IsStructureDirty() const { return m_isStructureDirty; }

        // Dirty フラグを下ろす（ECSContext が再解決後に呼ぶ）
        void ResetStructureDirty() { m_isStructureDirty = false; }


    private:
        // ----------------------------------------------------------
        // 内部ヘルパー
        // ----------------------------------------------------------

        // デバッグ用フォールバック取得（キャッシュミス用）
        template <class T>
        inline T* GetComponentDebug(EntityID entity);

        /**
         * @brief 削除されたエンティティの ID を世代インクリメントして再利用
         *        プールへ戻す。
         */
        void RecycleEntityID(EntityID id);


    private:
        // ----------------------------------------------------------
        // データメンバ
        // ----------------------------------------------------------

        FrameAllocator m_frameAllocator; // フレーム単位リセット型の高速アロケータ
        ChunkManager   m_chunkManager;   // Chunk 群の管理
        PendingOps     m_pendingOps;     // 遅延実行キュー
        EventBus       m_eventBus;       // World 専属のイベント配信ハブ

        // --- 世代管理 ---
        std::vector<uint32_t> m_entityGenerations;     // インデックスごとの現世代
        std::vector<uint32_t> m_freeIndices;           // 再利用待ちインデックス
        uint32_t              m_nextUnusedIndex = 1;   // 未使用領域の先頭インデックス

        bool m_isStructureDirty = true; // 構造変更フラグ（ECSContext と連動）

        // 型消去で保持されるリソース群（GridResource 等）
        std::vector<std::unique_ptr<ResourceBase>> m_resourcesArray;
    };


    // ============================================================
    // Inline Implementations
    // ------------------------------------------------------------
    // ここから下は World の完全定義が必要な実装です。
    // EntityRef / ComponentHandle のテンプレ実装も、World を完全に知った
    // 後でないと書けないため、ここに集約しています。
    // ============================================================

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
    inline T* World::GetComponentDebug(EntityID entity)
    {
        if (!IsEntityValid(entity)) return nullptr;

        size_t entityIdxInChunk = 0;
        size_t chunkIdx = m_chunkManager.SearchEntityIn(entity, &entityIdxInChunk);
        if (chunkIdx == InvalidIndex) return nullptr;

        auto& chunk = m_chunkManager.GetChunks()[chunkIdx];

        // 削除予約済みなら無効
        if (chunk->IsEntityDestroyed(entityIdxInChunk)) return nullptr;

        return static_cast<T*>(
            chunk->GetComponentPtrByType(TypeInfo<T>::ID(), entityIdxInChunk));
    }


    template <class T>
    inline T* World::GetComponent(EntityID entity)
    {
        if (!IsEntityValid(entity)) return nullptr;
        return static_cast<T*>(GetComponentOfEntity(entity, TypeInfo<T>::ID()));
    }


    template <class... Components>
    inline std::vector<EntityID> World::View()
    {
        std::vector<EntityID> entities;

        // 1. 欲しい構成を作る
        Archetype filter = ArchetypeHelper::Generate<Components...>();

        // 2. 全 Chunk を走査
        auto& chunks = m_chunkManager.GetChunks();
        for (const auto& upChunk : chunks) {
            if (!upChunk) continue;
            Chunk* chunk = upChunk.get();

            // 3. Chunk が filter を包含していれば、その全エンティティを採用
            if (ArchetypeHelper::LhasR(chunk->GetArchetype(), filter)) {
                const size_t   count = chunk->GetEntityCount();
                const EntityID* ids  = chunk->GetEntityIDs();
                entities.insert(entities.end(), ids, ids + count);
            }
        }
        return entities;
    }


    template <class... Components, class Func>
    inline void World::ViewEach(Func&& fn)
    {
        const Archetype filter = ArchetypeHelper::Generate<Components...>();

        for (const auto& upChunk : m_chunkManager.GetChunks()) {
            if (!upChunk) continue;
            Chunk* chunk = upChunk.get();
            if (!ArchetypeHelper::LhasR(chunk->GetArchetype(), filter)) continue;

            const size_t    count         = chunk->GetEntityCount();
            const EntityID* ids           = chunk->GetEntityIDs();
            const bool      hasDestroyed  = chunk->HasDestroyedEntities();

            for (size_t i = 0; i < count; ++i) {
                // RemoveEntity で空いた席（穴は CompactHoles まで詰められない）
                if (ids[i] == InvalidEntityID) continue;
                // 削除予約済み。hasDestroyed が false の間は判定自体を飛ばす
                if (hasDestroyed && chunk->IsEntityDestroyed(i)) continue;

                fn(ids[i]);
            }
        }
    }


    template <typename T>
    inline T* World::GetSingletonComponent()
    {
        const Archetype filter = ArchetypeHelper::Generate<T>();
        const TypeID    tid    = TypeInfo<T>::ID();

        // 1件見つけた時点で返す。vector に貯めてから先頭を取り直す必要はない。
        for (const auto& upChunk : m_chunkManager.GetChunks()) {
            if (!upChunk) continue;
            Chunk* chunk = upChunk.get();
            if (!ArchetypeHelper::LhasR(chunk->GetArchetype(), filter)) continue;

            const size_t    count        = chunk->GetEntityCount();
            const EntityID* ids          = chunk->GetEntityIDs();
            const bool      hasDestroyed = chunk->HasDestroyedEntities();

            for (size_t i = 0; i < count; ++i) {
                if (ids[i] == InvalidEntityID) continue;
                if (hasDestroyed && chunk->IsEntityDestroyed(i)) continue;

                return static_cast<T*>(chunk->GetComponentPtrByType(tid, i));
            }
        }
        return nullptr;
    }


    // ----- EntityRef のテンプレ実装（World の完全定義が必要なため、ここ） -----

    template <typename T>
    inline EntityRef& EntityRef::Set(T&& component)
    {
        world->AddComponent<std::decay_t<T>>(id, std::forward<T>(component));
        return *this;
    }

    template <typename T, typename Func>
    inline EntityRef& EntityRef::Patch(Func&& func)
    {
        world->PatchComponent<T>(id, std::forward<Func>(func));
        return *this;
    }


    // ----- ComponentHandle のテンプレ実装（同上） -----

    template <typename T>
    inline T* ComponentHandle<T>::Get() const
    {
        if (!m_world) return nullptr;
        return m_world->GetComponent<T>(m_id);
    }

    template <typename T>
    inline bool ComponentHandle<T>::IsValid() const
    {
        return Get() != nullptr;
    }


    // ----- World::Spawn の実装 -----
    inline EntityRef World::Spawn(const Archetype& archetype)
    {
        EntityID id = RequestSpawnEntity(archetype);
        return EntityRef{ this, id };
    }

} // namespace CCL::ECS
