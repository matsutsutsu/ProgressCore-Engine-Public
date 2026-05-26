/**
 * @file   CCL_World.h
 * @brief  ECSの中心クラス: Chunk群、PendingOps、EventBus、FrameAllocatorを統括
 *
 * @details
 *  Worldは1つのECS「世界」を表します。Spawn/Destroy/AddComponent等の
 *  ユーザーAPIはすべてここに集約され、内部的には PendingOps へ予約だけが
 *  記録されます。実際の構造変更は ScrutinyAndApply() で一括適用されます。
 *
 *  @par 主要API
 *   - RequestSpawnEntity()    : 即時 EntityID を発行(構造変更は予約のみ)
 *   - RequestDestroyEntity()  : 削除予約
 *   - AddComponent<T>()       : コンポーネント追加予約
 *   - PatchComponent<T>()     : 既存コンポーネントを書き換え予約
 *   - GetComponent<T>()       : 即時参照(現フレームのデータ)
 *   - ScrutinyAndApply()      : 予約をまとめて適用
 *
 *  【DestroyTag依存の解消】
 *   ゲーム固有のタグに依存していた旧設計から脱却し、RegisterDestroyTag() で
 *   「このタグTypeIDを持つEntityは自動削除する」とゲーム側から登録する形式に変更。
 *
 *  【依存】
 *   - CCL_ChunkManager.h, CCL_PendingOps.h, CCL_FrameAllocator.h, CCL_EventBus.h
 *   - \<unordered_map\>, \<vector\>, \<memory\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_ChunkManager.h"
#include "CCL_PendingOps.h"
#include "CCL_FrameAllocator.h"
#include "CCL_EventBus.h"

#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <cstring>
#include <cassert>


namespace CCL::ECS::Core
{
    using namespace CCL::ECS;

    class World; // 前方宣言(EntityRefから参照)


    // =========================================================================
    //  EntityRef : 流暢インターフェース用のEntity参照
    // =========================================================================

    /**
     * @struct EntityRef
     * @brief  Worldに紐づいた軽量Entityラッパ(チェーン記法用)
     *
     * @par 使用例
     * @code
     *     world.Spawn<Position, Velocity>()
     *          .Set(Position{0, 0, 0})
     *          .Set(Velocity{1, 0, 0});
     * @endcode
     */
    struct EntityRef
    {
        World*   world = nullptr;
        EntityID id    = InvalidEntityID;

        /// @brief コンポーネントを設定(AddComponent予約)
        template <typename T>
        EntityRef& Set(T&& component);

        /// @brief 既存コンポーネントをラムダで書き換え予約
        template <typename T, typename Func>
        EntityRef& Patch(Func&& func);

        operator EntityID() const noexcept { return id; }
        EntityID ID() const noexcept       { return id; }
    };


    // =========================================================================
    //  Resource: 型ごとに1個保持される共有データ
    // =========================================================================

    class ResourceBase
    {
    public:
        virtual ~ResourceBase() = default;
    };

    template <typename T>
    class ResourceWrapper : public ResourceBase
    {
    public:
        template <typename... Args>
        explicit ResourceWrapper(Args&&... args) : value(std::forward<Args>(args)...) {}
        T value;
    };

    /**
     * @class ResourceIDGenerator
     * @brief 型ごとに一意なリソースIDを発行
     */
    class ResourceIDGenerator
    {
    public:
        template <typename T>
        static std::uint32_t GetID() noexcept
        {
            static const std::uint32_t id = NextID();
            return id;
        }
    private:
        static std::uint32_t NextID() noexcept
        {
            static std::atomic<std::uint32_t> s_counter{0};
            return s_counter.fetch_add(1, std::memory_order_relaxed);
        }
    };


    // =========================================================================
    //  World
    // =========================================================================

    /**
     * @class World
     * @brief ECS全体の中央管理クラス
     */
    class World
    {
    public:
        World();
        ~World();

        World(const World&)            = delete;
        World& operator=(const World&) = delete;


        // =====================================================================
        //  ライフサイクル
        // =====================================================================

        /// @brief 全Entity・全予約・全イベント購読を破棄(リソースは保持)
        void Clear();


        // =====================================================================
        //  Spawn / Destroy
        // =====================================================================

        /**
         * @brief 新規Entityを発行予約する(IDは即座に返る)
         * @param archetype 初期コンポーネント構成
         * @return 即時利用可能な世代付きEntityID
         */
        EntityID RequestSpawnEntity(const Archetype& archetype);

        /**
         * @brief 流暢な Entity生成 API
         * @tparam Components 初期コンポーネント型
         * @return EntityRef (チェーン操作可能)
         */
        template <class... Components>
        EntityRef Spawn()
        {
            const Archetype arch = ArchetypeHelper::Generate<Components...>();
            const EntityID id    = RequestSpawnEntity(arch);
            return EntityRef{this, id};
        }

        /// @brief Entityを削除予約
        void RequestDestroyEntity(EntityID entity);


        // =====================================================================
        //  Component 操作
        // =====================================================================

        /**
         * @brief デフォルト構築でコンポーネントTを追加予約
         * @tparam T 追加する型
         */
        template <class T>
        void AddComponent(EntityID entity)
        {
            if (!IsEntityValid(entity)) return;

            TypeData td;
            td.Create<T>();

            auto ctor = [](void* ptr) { new (ptr) T(); };
            RequestAddComponent(entity, td.id, td.typeSize, ctor,
                td.destructor, td.constructor, td.assigner, td.mover, td.name);
        }

        /**
         * @brief 引数付きでコンポーネントTを追加予約 (Emplace)
         * @tparam T    追加する型
         * @tparam Args コンストラクタ引数
         * @details ムーブのみ可能な型(unique_ptrを持つ型など)もサポート
         */
        template <class T, typename... Args>
        void AddComponent(EntityID entity, Args&&... args)
        {
            if (!IsEntityValid(entity)) return;

            TypeData td;
            td.Create<T>();

            // ★引数をキャプチャしてfactoryで構築
            auto tuple = std::make_shared<std::tuple<std::decay_t<Args>...>>(
                std::forward<Args>(args)...);

            auto factory = [tuple](void* ptr) {
                std::apply([ptr](auto&&... a) {
                    new (ptr) T(std::forward<decltype(a)>(a)...);
                }, *tuple);
            };

            RequestAddComponent(entity, td.id, td.typeSize, factory,
                td.destructor, td.constructor, td.assigner, td.mover, td.name);
        }

        /// @brief 型Tを削除予約
        template <class T>
        void RequestRemoveComponent(EntityID entity)
        {
            RequestRemoveComponent(entity, TypeInfo<T>::ID());
        }

        /**
         * @brief 既存コンポーネントの中身をラムダで書き換え予約
         * @tparam T    対象型
         * @param  entity 対象Entity
         * @param  action 書き換え関数
         */
        template <class T>
        void PatchComponent(EntityID entity, std::function<void(T&)> action)
        {
            if (!IsEntityValid(entity)) return;

            PendingOp op;
            op.kind     = PendingOpKind::PatchComponent;
            op.entity   = entity;
            op.type     = TypeInfo<T>::ID();
            op.typeSize = TypeInfo<T>::Size();
            op.patcher  = [action = std::move(action)](void* ptr) {
                action(*reinterpret_cast<T*>(ptr));
            };
            _pendingOps.Add(std::move(op));
        }


        // =====================================================================
        //  即時参照 (現フレームの確定データ)
        // =====================================================================

        /// @brief 指定型のコンポーネントポインタを取得(なければnullptr)
        template <class T>
        T* GetComponent(EntityID entity)
        {
            return reinterpret_cast<T*>(GetComponentOfEntity(entity, TypeInfo<T>::ID()));
        }

        /// @brief 指定型を持つか判定
        template <class T>
        bool HasComponent(EntityID entity)
        {
            return GetComponent<T>(entity) != nullptr;
        }


        // =====================================================================
        //  Entity 有効性
        // =====================================================================

        /// @brief EntityIDが現世代として有効か(世代チェック)
        bool IsEntityValid(EntityID entity) const noexcept
        {
            const std::uint32_t idx = EntityHandle::GetIndex(entity);
            const std::uint32_t gen = EntityHandle::GetGeneration(entity);
            if (idx >= _entityGenerations.size()) return false;
            return _entityGenerations[idx] == gen;
        }

        /// @brief 指定EntityのArchetype参照(なければ空Archetype)
        const Archetype& GetEntityArchetype(EntityID entity) const;


        // =====================================================================
        //  リソース管理 (世界に1個だけのデータ)
        // =====================================================================

        template <typename T, typename... Args>
        void AddResource(Args&&... args)
        {
            const auto id = ResourceIDGenerator::GetID<T>();
            _resources[id] = std::make_unique<ResourceWrapper<T>>(std::forward<Args>(args)...);
        }

        template <typename T>
        T& GetResource()
        {
            const auto id = ResourceIDGenerator::GetID<T>();
            auto it = _resources.find(id);
            assert(it != _resources.end() && "Resource not found");
            return static_cast<ResourceWrapper<T>*>(it->second.get())->value;
        }

        template <typename T>
        bool HasResource() const
        {
            return _resources.count(ResourceIDGenerator::GetID<T>()) > 0;
        }


        // =====================================================================
        //  構造変更の確定 (フレーム末で呼ばれる)
        // =====================================================================

        /// @brief 蓄積された予約を一括適用し、ChunkManagerに反映する
        void ScrutinyAndApply();


        // =====================================================================
        //  アクセサ
        // =====================================================================

        ChunkManager& GetChunkManager()              { return _chunkManager; }
        const ChunkManager& GetChunkManager() const  { return _chunkManager; }

        PendingOps&    GetPendingOps()    { return _pendingOps; }
        EventBus&      GetEventBus()      { return _eventBus; }
        FrameAllocator& GetFrameAllocator(){ return _frameAllocator; }

        bool IsStructureDirty() const noexcept { return _isStructureDirty; }
        void ResetStructureDirty() noexcept    { _isStructureDirty = false; }


        // =====================================================================
        //  非テンプレート版API (テンプレからの委譲先)
        // =====================================================================

        void RequestAddComponent(
            EntityID                          entity,
            TypeID                            tid,
            std::size_t                       size,
            const std::function<void(void*)>& ctor,
            const Destructor&                 dtor,
            TypeData::ConstructFunc           copyCtor,
            TypeData::AssignFunc              assigner,
            TypeData::MoveFunc                mover,
            const char*                       name);

        void RequestRemoveComponent(EntityID entity, TypeID tid);

    private:
        /// @brief 内部: 型情報を持たない一般的なコンポーネント取得
        void* GetComponentOfEntity(EntityID entity, TypeID tid);

        /// @brief 内部: EntityIDの世代をインクリメントして再利用キューへ
        void RecycleEntityID(EntityID id);


    private:
        // =====================================================================
        //  メンバ
        // =====================================================================

        ChunkManager    _chunkManager;
        PendingOps      _pendingOps;
        FrameAllocator  _frameAllocator;
        EventBus        _eventBus;

        /// @brief 世代番号テーブル (index → 現世代)
        std::vector<std::uint32_t> _entityGenerations;

        /// @brief 再利用可能なスロット番号
        std::vector<std::uint32_t> _freeIndices;

        /// @brief 次に割り当てる新規スロット番号
        std::uint32_t _nextUnusedIndex = 1;

        /// @brief 構造変更があったフラグ(System側でクエリ再構築のため)
        bool _isStructureDirty = false;

        /// @brief リソースストア (型ID → リソース)
        std::unordered_map<std::uint32_t, std::unique_ptr<ResourceBase>> _resources;
    };


    // =========================================================================
    //  EntityRef 実装 (Worldの定義が必要なため末尾)
    // =========================================================================

    template <typename T>
    inline EntityRef& EntityRef::Set(T&& component)
    {
        if (world) {
            world->AddComponent<std::decay_t<T>>(id, std::forward<T>(component));
        }
        return *this;
    }

    template <typename T, typename Func>
    inline EntityRef& EntityRef::Patch(Func&& func)
    {
        if (world) {
            world->PatchComponent<T>(id,
                std::function<void(T&)>(std::forward<Func>(func)));
        }
        return *this;
    }

} // namespace CCL::ECS::Core
