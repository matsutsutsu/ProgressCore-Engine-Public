// TransformInterpolationSystem.cpp
#include "TransformInterpolationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/RenderInterpState.h"
#include "Engine/Transform/Core/TransformHierarchyCache.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

// ローカルのprevPosition/positionをalphaで補間し、ローカル行列を合成する。
//
// 静的物体は補間不要のため、現在のローカル値をそのまま使う。
static inline XMMATRIX ComposeLocalRender(const TransformComponent &t, float alpha)
{
    if (t.isStatic) {
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

    // ---------------------------------------------------------------
    // Phase 1: ルート(parent==0)を並列で補間確定。
    //          TransformUpdateSystem::ComputeRootsParallel と対になる処理。
    // ---------------------------------------------------------------
    ForEachParallel([alpha](TransformComponent &trans) {
        if (trans.parent != 0) return; // 子は Phase 2 で処理
        XMMATRIX local = ComposeLocalRender(trans, alpha);
        XMStoreFloat4x4(&trans.renderWorldMatrix, local); // ルートはローカル＝ワールド
    });

    // ---------------------------------------------------------------
    // Phase 2: 非ルートをdepth順に「補間ローカル × 親renderWorldMatrix」で解決。
    //          TransformUpdateSystem::SolveHierarchy と対になる処理。
    //          cache自体はLogic権威データなので読み取り専用で使い、
    //            書き込みは自前の m_renderWorldDense に対してのみ行う。
    // ---------------------------------------------------------------
    if (!m_world->HasResource<TransformHierarchyCache*>()) return;
    TransformHierarchyCache *cache = m_world->GetResource<TransformHierarchyCache*>();
    if (!cache) return;

    const size_t n = cache->Count();
    if (m_renderWorldDense.size() < n) m_renderWorldDense.resize(n);

    for (size_t i = 0; i < n; ++i) {
        const int idx = static_cast<int>(i);
        const EntityID id = cache->EntityAt(idx);

        auto *t = m_world->GetComponent<TransformComponent>(id);
        if (!t) continue;

        const XMMATRIX local = ComposeLocalRender(*t, alpha);

        // 親のrenderWorldMatrix取得：非ルート親なら自前denseから、ルート親ならPhase1で確定済みのコンポーネントから
        const int p = cache->ParentInOrderOf(idx);
        XMMATRIX parentW;
        if (p >= 0) {
            parentW = XMLoadFloat4x4(&m_renderWorldDense[p]);
        }
        else {
            auto *pt = m_world->GetComponent<TransformComponent>(cache->ParentIdOf(idx));
            parentW = pt ? XMLoadFloat4x4(&pt->renderWorldMatrix) : XMMatrixIdentity();
        }

        const XMMATRIX world = XMMatrixMultiply(local, parentW);
        XMStoreFloat4x4(&m_renderWorldDense[idx], world); // 後続の子が読むため保持
        XMStoreFloat4x4(&t->renderWorldMatrix, world);   // 描画用に書き戻し
    }
}

REGISTER_RENDER_SYSTEM(TransformInterpolationSystem, Priority::RenderStage::R00_Interpolation);
