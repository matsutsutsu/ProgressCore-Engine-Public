#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Core/TransformHierarchyCache.h"  
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/Components/DestroyTag.h"

// 前方宣言
namespace CCL::ECS::Core { class World; }


class TransformUpdateSystem : public CCL::ECS::IfSystem<TransformUpdateSystem,CCL::ECS::Write<TransformComponent>> {
  public:
    TransformUpdateSystem();
    virtual ~TransformUpdateSystem() = default;

    /// @brief 登録時に1回だけ呼ばれる。階層解決キャッシュをResourceとして公開する。
    /// @note TransformInterpolationSystem(Render Stage)が同じdepth順を読むために使う。
    ///       所有権は本システムのまま（_cacheへの生ポインタを渡すだけ）。
    void Initialize() override;


    // フレーム毎の更新処理
    void Update(float dt) override;


    // --- 親子関係操作用ヘルパー関数 (static) ---
    // 外部（PrefabやLogic）から TransformUpdateSystem::SetParent(...) として呼び出します
    static void SetParent(
        CCL::ECS::Core::World &world, CCL::ECS::EntityID child, CCL::ECS::EntityID parent);

    // 親から子を切り離す
    static void DetachChild(CCL::ECS::Core::World &world, CCL::ECS::EntityID child);

    // 階層構造を安全に維持しながら削除予約する
    // destroyChildren: trueなら子供も道連れにする（通常はtrue推奨）
    static void DestroyEntityWithHierarchy(
        CCL::ECS::Core::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);


    // 名前を変更: DestroyEntityWithHierarchy -> MarkForDestruction
    // 直接消すのではなく、「削除タグ」を再帰的に付与する関数に変更
    static void MarkForDestruction(
        CCL::ECS::Core::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);

  private:
    // 保留中の親子関係を適用する関数
    void ProcessPendingParents();

    // リンク切れを修復する関数（メインスレッド実行用）
    void RepairBrokenLinks();

    /// @brief Chunk 線形 1 パス。compose は行わず、トポロジ変化検出とゴースト修復のみ。
    bool ScanDetectTopology();

    /// @brief ルート（parent==0）を並列・インプレースで計算。フラットな大多数はここで完結。
    void ComputeRootsParallel();

    /// @brief 非ルート（少数）を depth 順に compose×親world で解決し、その場で書き戻す。
    void SolveHierarchy();

    TransformHierarchyCache _cache;
};