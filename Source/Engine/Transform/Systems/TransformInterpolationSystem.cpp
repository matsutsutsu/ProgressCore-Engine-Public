#include "TransformInterpolationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/RenderInterpState.h"
#include "Engine/Transform/Core/TransformHierarchyCache.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

/**
 * @brief ローカルの前ステップ値と現在値を alpha で補間し、ローカル行列を合成する。
 * @param t     補間する Transform。静的物体は動かないので補間せず、現在の値をそのまま使う。
 * @param alpha 補間の割合（0=前ステップ, 1=現在）。
 * @return 補間したローカル行列。
 */
static inline XMMATRIX ComposeLocalRender(const TransformComponent &t, float alpha)
{
    if (t.isStatic)
    {
        return XMMatrixAffineTransformation(
            XMLoadFloat3(&t.scale), XMVectorZero(),
            XMLoadFloat4(&t.rotation), XMLoadFloat3(&t.position));
    }
    XMVECTOR lerpPos  = XMVectorLerp(XMLoadFloat3(&t.prevPosition), XMLoadFloat3(&t.position), alpha);
    XMVECTOR slerpRot = XMQuaternionSlerp(XMLoadFloat4(&t.prevRotation), XMLoadFloat4(&t.rotation), alpha);
    return XMMatrixAffineTransformation(XMLoadFloat3(&t.scale), XMVectorZero(), slerpRot, lerpPos);
}

void TransformInterpolationSystem::Update(float dt)
{
    if (!m_world->HasResource<RenderInterpState>()) return;
    const float alpha = m_world->GetResource<RenderInterpState>().alpha;

    // Phase 1: ルート（parent == 0）を並列に補間して確定する。
    //          TransformUpdateSystem::ComputeRootsParallel と対になる処理
    ForEachParallel([alpha](TransformComponent &trans) {
        if (trans.parent != 0) return; // 子は Phase 2 で処理
        XMMATRIX local = ComposeLocalRender(trans, alpha);
        XMStoreFloat4x4(&trans.renderWorldMatrix, local); // ルートはローカル＝ワールド
    });

    // Phase 2: 非ルートを親が先の順に「補間したローカル × 親の renderWorldMatrix」で解決する。
    //          TransformUpdateSystem::SolveHierarchy と対になる処理。
    //          キャッシュはロジック用のデータなので読むだけにし、
    //          中間結果は自前の m_renderWorldDense に書く
    if (!m_world->HasResource<TransformHierarchyCache *>()) return;
    TransformHierarchyCache *cache = m_world->GetResource<TransformHierarchyCache *>();
    if (!cache) return;

    const size_t n = cache->Count();
    if (m_renderWorldDense.size() < n) m_renderWorldDense.resize(n);

    for (size_t i = 0; i < n; ++i)
    {
        const int      idx = static_cast<int>(i);
        const EntityID id  = cache->EntityAt(idx);

        auto *t = m_world->GetComponent<TransformComponent>(id);
        if (!t) continue;

        const XMMATRIX local = ComposeLocalRender(*t, alpha);

        // 親の renderWorldMatrix: 親が非ルートなら自前の作業領域から、ルートなら Phase 1 で確定したコンポーネントから
        const int p = cache->ParentInOrderOf(idx);
        XMMATRIX  parentW;
        if (p >= 0)
        {
            parentW = XMLoadFloat4x4(&m_renderWorldDense[p]);
        }
        else
        {
            auto *pt = m_world->GetComponent<TransformComponent>(cache->ParentIdOf(idx));
            parentW  = pt ? XMLoadFloat4x4(&pt->renderWorldMatrix) : XMMatrixIdentity();
        }

        const XMMATRIX world = XMMatrixMultiply(local, parentW);
        XMStoreFloat4x4(&m_renderWorldDense[idx], world); // 後続の子が読むため保持
        XMStoreFloat4x4(&t->renderWorldMatrix, world);    // 描画用に書き戻し
    }
}

REGISTER_RENDER_SYSTEM(TransformInterpolationSystem, Priority::RenderStage::R00_Interpolation);
