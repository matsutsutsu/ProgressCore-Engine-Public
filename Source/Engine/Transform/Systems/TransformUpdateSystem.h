#pragma once
// ===========================================================================
// 概要: 親子関係を解決して全 Transform の worldMatrix を確定する System（L02_PostUpdate）。
//
// 責務:
//   - ルート（親なし）を並列に、子孫を親が先の順に計算して worldMatrix を確定する
//   - 親子関係の変化を検出して TransformHierarchyCache を作り直す
//   - 親子関係の操作（SetParent / DetachChild）と、階層ごとの破棄の予約
//   - 物理から返ったワールド値をローカル値へ書き戻すヘルパー
//
// 備考: 大半の Entity は親を持たないので、ルートだけを並列に処理すれば済むようにしている。
//       子孫は少数の前提で、キャッシュした順に直列で解決する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Core/TransformHierarchyCache.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/Components/DestroyTag.h"

namespace CCL::ECS
{
    class World;
}

class TransformUpdateSystem : public CCL::ECS::IfSystem<TransformUpdateSystem, CCL::ECS::Write<TransformComponent>>
{
public:
    TransformUpdateSystem();
    virtual ~TransformUpdateSystem() = default;

    /**
     * @brief 階層解決キャッシュを World リソースとして公開する。
     *
     * TransformInterpolationSystem（Render ステージ）が同じ並び順を読むために使う。
     *
     * @note 所有権はこのシステムのまま（m_cache への生ポインタを渡すだけ）。
     */
    void Initialize() override;

    /**
     * @brief 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
     *
     * 本システムは親子関係を解決してワールド行列を確定させる、L02_PostUpdate の締めくくり。
     * 同ステージでローカル Transform を書き換えるシステムより後に走らないと、確定したはずの行列が後から書き換わる。
     */
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

    // 親子リンクを外しながら破棄を予約する。
    // destroyChildren が true なら子孫も破棄し、false なら子はルートにして残す
    static void DestroyEntityWithHierarchy(
        CCL::ECS::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);

    // entity（と、destroyChildren が true なら子孫）に DestroyTag を付ける
    static void MarkForDestruction(
        CCL::ECS::World &world, CCL::ECS::EntityID entity, bool destroyChildren = true);

    // ============================================================
    // 物理との受け渡し
    // ============================================================

    /**
     * @brief 物理から返ってきた「ワールド座標・回転」を position/rotation へ正しく書き戻す。
     *
     * @details
     *  position / rotation は **親からの相対値(Local)**。Jolt が返すのはワールド値なので、
     *  親を持つ Entity にそのまま代入すると親の変換が二重に掛かって飛ぶ。
     *  親がいるときだけ親の worldMatrix の逆変換を通してローカルへ落とす。
     *
     *  物理へ渡す側（JoltBodySetupSystem / JoltPushSystem）は GetWorldPosition() /
     *  GetWorldRotation() でワールド値を渡しているので、戻す側もワールド値として扱う。
     *
     * @note 親の worldMatrix を読む。L05（Pull）の時点で worldMatrix を書くシステムは
     *       無いため、並列ループから呼んでも安全。
     */
    static void WriteWorldTransform(CCL::ECS::World &world, CCL::ECS::EntityID entity,
                                    TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos,
                                    const DirectX::XMFLOAT4 &worldRot);

    // 回転は変えず、ワールド座標だけ書き戻す版（CharacterVirtual 用）
    static void WriteWorldPosition(CCL::ECS::World &world, CCL::ECS::EntityID entity,
                                   TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos);

private:
    // PendingParentComponent で予約された親子付けを適用する
    void ProcessPendingParents();

    // 親は指しているのに兄弟リストに繋がっていない子を、SetParent で繋ぎ直す
    void RepairBrokenLinks();

    /**
     * @brief 全 Transform を1回だけ走査し、親子関係の変化を検出する（行列の計算はしない）。
     *
     * 破棄済みの親を指したままのリンク（ゴーストリンク）もここで外す。
     *
     * @return キャッシュの作り直しが必要なら true。
     */
    bool ScanDetectTopology();

    // ルート（parent == 0）を並列にその場で計算する。親を持たない大多数はここで終わる
    void ComputeRootsParallel();

    // 非ルート（少数）を親が先の順に「ローカル × 親のワールド」で解決し、その場で書き戻す
    void SolveHierarchy();

    TransformHierarchyCache m_cache;
};
