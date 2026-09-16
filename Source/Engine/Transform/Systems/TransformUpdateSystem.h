#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Core/TransformHierarchyCache.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/Components/DestroyTag.h"

// 前方宣言
namespace CCL::ECS
{
    class World;
}

class TransformUpdateSystem : public CCL::ECS::IfSystem<TransformUpdateSystem, CCL::ECS::Write<TransformComponent>>
{
public:
    TransformUpdateSystem();
    virtual ~TransformUpdateSystem() = default;

    // 登録時に1回だけ呼ばれる。階層解決キャッシュをResourceとして公開する。
    //
    // TransformInterpolationSystem(Render Stage)が同じdepth順を読むために使う。
    // 所有権は本システムのまま（m_cacheへの生ポインタを渡すだけ）。
    void Initialize() override;

    // フレーム毎の更新処理

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // 本システムは親子関係を解決してワールド行列を確定させる、L02_PostUpdate の
    // 締めくくり。同ステージでローカル Transform を書き換えるシステムより後に
    // 走らないと、確定したはずの行列が後から書き換わる。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BossActionSystem"};
    }

    void Update(float dt) override;

    // --- 親子関係操作用ヘルパー関数 (static) ---
    // 外部（PrefabやLogic）から TransformUpdateSystem::SetParent(...) として呼び出します
    static void SetParent(
        CCL::ECS::World &world, CCL::ECS::EntityID child, CCL::ECS::EntityID parent);

    // 親から子を切り離す
    static void DetachChild(CCL::ECS::World &world, CCL::ECS::EntityID child);

    // 階層構造を安全に維持しながら削除予約する
    // destroyChildren: trueなら子供も道連れにする（通常はtrue推奨）
    static void DestroyEntityWithHierarchy(
        CCL::ECS::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);

    // DestroyTagを引数のEntityの子供含めて全て立てる処理
    static void MarkForDestruction(
        CCL::ECS::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);

    /**
     * @brief 物理から返ってきた「ワールド座標・回転」を position/rotation へ正しく書き戻す。
     *
     * @details
     *  position / rotation は **親からの相対値(Local)**。Jolt が返すのはワールド値なので、
     *  親を持つ Entity にそのまま代入すると親の変換が二重に掛かって飛ぶ。
     *  親がいるときだけ親の worldMatrix の逆変換を通してローカルへ落とす。
     *
     *  生成側(JoltBodySetupSystem / JoltPushSystem)は GetWorldPosition() /
     *  GetWorldRotation() を使ってワールドで出し入れしているのに、戻す側だけが
     *  ローカル欄へワールド値を直接書いていた。その非対称をここで閉じる。
     *
     * @note 親の worldMatrix を読む。L05(Pull)の時点で worldMatrix を書くシステムは
     *       無いため、並列ループから呼んでも安全。
     */
    static void WriteWorldTransform(CCL::ECS::World &world, CCL::ECS::EntityID entity,
                                    TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos,
                                    const DirectX::XMFLOAT4 &worldRot);

    /// 回転は変えずワールド座標だけ書き戻す版（CharacterVirtual 用）。
    static void WriteWorldPosition(CCL::ECS::World &world, CCL::ECS::EntityID entity,
                                   TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos);

private:
    // 保留中の親子関係を適用する関数
    void ProcessPendingParents();

    // リンク切れを修復する関数（メインスレッド実行用）
    void RepairBrokenLinks();

    // Chunk 線形 1 パス。compose は行わず、トポロジ変化検出とゴースト修復のみ。
    bool ScanDetectTopology();

    // ルート（parent==0）を並列・インプレースで計算。フラットな大多数はここで完結。
    void ComputeRootsParallel();

    // 非ルート（少数）を depth 順に compose×親world で解決し、その場で書き戻す。
    void SolveHierarchy();

    TransformHierarchyCache m_cache;
};
