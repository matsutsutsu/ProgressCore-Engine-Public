#include "TransformInterpolationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/RenderInterpState.h"
#include "Engine/Transform/Core/TransformHierarchyCache.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

// ローカルの前ステップの値と今の値を alpha（0 が前ステップ、1 が今）で補間し、ローカル行列にする。静的な物は補間しない
// 行列でなく TRS を補間する。行列の成分を補間すると、回転の途中で縮んだり歪んだりする
// ワールドでなくローカルを補間するのは、親と一緒に回る子が弧を描くようにするため（ワールドの位置の補間だと弦の上を通る）
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
    // 処理フロー:
    // 1. 補間の割合（alpha）を取る
    // 2. ルートを並列に補間して確定する
    // 3. 非ルートを親が先の順に解決する

    // ---------------------------------------------------------
    // 1. alpha
    // ---------------------------------------------------------

    // alpha は Framework::Run が毎フレーム書く（0 = 1つ前のステップ、1 = 最新のステップ）
    if (!m_world->HasResource<RenderInterpState>()) return;
    const float alpha = m_world->GetResource<RenderInterpState>().alpha;

    // ---------------------------------------------------------
    // 2. ルート
    // ---------------------------------------------------------

    // TransformUpdateSystem::ComputeRootsParallel と対になる処理
    ForEachParallel([alpha](TransformComponent &trans) {
        if (trans.parent != 0) return; // 子は 3 で処理する
        XMMATRIX local = ComposeLocalRender(trans, alpha);
        XMStoreFloat4x4(&trans.renderWorldMatrix, local); // ルートはローカル＝ワールド
    });

    // ---------------------------------------------------------
    // 3. 非ルート
    // ---------------------------------------------------------

    // TransformUpdateSystem::SolveHierarchy と対になる処理
    // 「補間したローカル × 親の renderWorldMatrix」を、キャッシュの並び（親が先）の順に計算する

    // キャッシュの作業領域（WorldAt / StoreWorld）にはロジック用の worldMatrix が入っている
    // 描画用の値で上書きしないよう、キャッシュは並び順を読むだけにし、途中の結果は m_renderWorldDense に書く
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
