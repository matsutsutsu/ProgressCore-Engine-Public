#pragma once
// ===========================================================================
// 概要: 親子関係を解決して全 Transform の worldMatrix を確定する System（L02_PostUpdate）
//
// 責務:
//   - ルート（親なし）を並列に、子孫を親が先の順に計算して worldMatrix を確定する
//   - 親子関係の変化を検出して TransformHierarchyCache を作り直す
//   - 親子関係の操作（SetParent / DetachChild）と、階層ごとの破棄の予約
//   - 物理から返ったワールド値をローカル値へ書き戻すヘルパー
//
// 備考: 大半の Entity は親を持たないので、ルートだけを並列に処理すれば済むようにしている
//       子孫は少数の前提で、キャッシュした順に直列で解決する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Core/TransformHierarchyCache.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/Components/DestroyTag.h"

namespace CCL::ECS
{
    class World;
}

class TransformUpdateSystem : public CCL::ECS::IfSystem<
    TransformUpdateSystem,
    CCL::ECS::Write<TransformComponent>>
{
public:
    TransformUpdateSystem() : IfSystem("TransformUpdateSystem") {}
    virtual ~TransformUpdateSystem() = default;

    // 階層の解決のキャッシュを World のリソースとして出す（TransformInterpolationSystem が同じ並びを読む）
    // 所有はこの System のまま
    void Initialize() override;

    // 親子を解決してワールド行列を確定させる、L02_PostUpdate の締め。ローカルの Transform を書く System より後に走る
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BossActionSystem"};
    }

    void Update(float dt) override;

    // ============================================================
    // 親子関係の操作（static。Prefab やゲームロジックから呼ぶ）
    // ============================================================

    // child を parent の子にする。parent が 0 なら親から切り離すだけ
    static void SetParent(
        CCL::ECS::World &world, CCL::ECS::EntityID child, CCL::ECS::EntityID parent);

    // 親から子を切り離す
    static void DetachChild(CCL::ECS::World &world, CCL::ECS::EntityID child);

    // ============================================================
    // 物理との受け渡し
    // ============================================================

    // 物理から返ったワールドの位置・回転を、ローカルの position / rotation へ書き戻す
    // 親がいるときは親の worldMatrix の逆で戻す。そのまま入れると、親の変換が二重に掛かる
    // 親の worldMatrix を読むが、L05 の時点でそれを書く System はないので、並列のループから呼んでよい
    static void WriteWorldTransform(CCL::ECS::World &world, CCL::ECS::EntityID entity,
                                    TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos,
                                    const DirectX::XMFLOAT4 &worldRot);

    // WriteWorldTransform の、回転を変えずワールド座標だけ書き戻す版（CharacterVirtual 用）
    static void WriteWorldPosition(CCL::ECS::World &world, CCL::ECS::EntityID entity,
                                   TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos);

    // ============================================================
    // 破棄の予約
    // ============================================================

    // 親子のリンクを外しながら破棄を予約する。destroyChildren が false なら、子はルートにして残す
    static void DestroyEntityWithHierarchy(
        CCL::ECS::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);

    // DestroyTag を付ける（消すのは L07 の HierarchyCleanupSystem）。destroyChildren なら子孫にも付ける
    static void MarkForDestruction(
        CCL::ECS::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);

private:
    // PendingParentComponent で予約された親子付けを適用する
    void ProcessPendingParents();

    // 親は指しているのに兄弟リストに繋がっていない子を、SetParent で繋ぎ直す
    void RepairBrokenLinks();

    // 全 Transform を1回だけ回り、親子の変化を見つける（行列は計算しない）。作り直しが要るなら true
    // 破棄済みの親を指したままのリンクも、ここで外す
    bool ScanDetectTopology();

    // ルート（parent == 0）を並列にその場で計算する。親を持たない大多数はここで終わる
    void ComputeRootsParallel();

    // 非ルート（少数）を親が先の順に「ローカル × 親のワールド」で解決し、その場で書き戻す
    void SolveHierarchy();

    TransformHierarchyCache m_cache;
};
