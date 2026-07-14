// ===========================================================================
// TrailSystem.cpp
// 役割・責務・位置付けは TrailSystem.h を参照。
//
// 実装の意図(Why):
//   - サンプルは renderWorldMatrix(補間済み)基準。ゲームロジックの worldMatrix ではなく
//     描画に見える位置に軌跡を合わせるため（ModelRenderSystem と同じ思想）。
//   - 「速く振ったときだけ尾が出る」剣らしさのため、先端が minDistance 以上動いたときのみ
//     新区間を刻む。静止時は追記されず、既存区間が lifeTime で自然に消える。
//   - リングは固定長。満杯時は最古を捨てて上書きし、ヒープ確保を避ける。
// ===========================================================================
#include "TrailSystem.h"

#include "Engine/Graphics/Renderer/TrailRenderer.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <DirectXMath.h>
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;

void TrailSystem::Update(float dt)
{
    auto& renderer = TrailRenderer::Instance();

    // リボン頂点の一時バッファ（エンティティ間で使い回してアロケーションを抑える）。
    // ForEach はシングルスレッド版のため関数ローカルで安全。
    std::vector<TrailRenderer::Vertex> verts;

    ForEach([&](const TransformComponent& transform, TrailComponent& trail) {

        // ── 1. 現在の刃2端点をワールド空間へ（w=1 で並進込み変換） ──
        const XMMATRIX W     = XMLoadFloat4x4(&transform.renderWorldMatrix);
        const XMVECTOR baseW = XMVector3Transform(XMLoadFloat3(&trail.baseOffset), W);
        const XMVECTOR tipW  = XMVector3Transform(XMLoadFloat3(&trail.tipOffset),  W);

        XMFLOAT3 baseF, tipF;
        XMStoreFloat3(&baseF, baseW);
        XMStoreFloat3(&tipF,  tipW);

        const int MAX = TrailComponent::MAX_SEGMENTS;

        // ── 2. 既存区間を老化させ、寿命切れを最古側から破棄 ──
        for (int i = 0; i < trail.count; ++i)
        {
            int idx = (trail.head + i) % MAX;
            trail.segments[idx].age += dt;
        }
        while (trail.count > 0 && trail.segments[trail.head].age >= trail.lifeTime)
        {
            trail.head = (trail.head + 1) % MAX;
            trail.count--;
        }

        // ── 3. 新区間を刻むか判定（emitting かつ 先端が十分動いた／初回） ──
        bool record = trail.emitting;
        if (record && trail.count > 0)
        {
            int newest = (trail.head + trail.count - 1) % MAX;
            XMVECTOR lastTip = XMLoadFloat3(&trail.segments[newest].tip);
            float movedSq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(tipW, lastTip)));
            if (movedSq < trail.minDistance * trail.minDistance)
                record = false;
        }
        if (record)
        {
            int writeIdx;
            if (trail.count < MAX)
            {
                writeIdx = (trail.head + trail.count) % MAX;
                trail.count++;
            }
            else
            {
                // 満杯: 最古を捨てて上書き
                writeIdx   = trail.head;
                trail.head = (trail.head + 1) % MAX;
            }
            trail.segments[writeIdx].base = baseF;
            trail.segments[writeIdx].tip  = tipF;
            trail.segments[writeIdx].age  = 0.0f;
        }

        // ── 4. 区間列からリボン(三角形リスト)を生成して提出 ──
        if (trail.count < 2)
            return;

        const float invLife = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;

        auto makeVert = [&](const XMFLOAT3& p, float age) -> TrailRenderer::Vertex {
            float fade = 1.0f - age * invLife; // 尾ほど薄い
            if (fade < 0.0f) fade = 0.0f;
            TrailRenderer::Vertex v;
            v.position = p;
            v.color    = XMFLOAT4(trail.color.x, trail.color.y, trail.color.z,
                                  trail.color.w * fade);
            return v;
        };

        verts.clear();
        verts.reserve((trail.count - 1) * 6);

        for (int i = 0; i < trail.count - 1; ++i)
        {
            int i0 = (trail.head + i) % MAX;
            int i1 = (trail.head + i + 1) % MAX;
            const TrailComponent::Segment& s0 = trail.segments[i0];
            const TrailComponent::Segment& s1 = trail.segments[i1];

            TrailRenderer::Vertex b0 = makeVert(s0.base, s0.age);
            TrailRenderer::Vertex t0 = makeVert(s0.tip,  s0.age);
            TrailRenderer::Vertex b1 = makeVert(s1.base, s1.age);
            TrailRenderer::Vertex t1 = makeVert(s1.tip,  s1.age);

            // 四角形 (b0,t0,t1,b1) を2三角形へ。加算＆両面なので巻き順は不問。
            verts.push_back(b0); verts.push_back(t0); verts.push_back(t1);
            verts.push_back(b0); verts.push_back(t1); verts.push_back(b1);
        }

        renderer.SubmitTriangleList(verts.data(), verts.size());
    });
}

// メイン描画ステージで区間更新＆頂点提出。実際の描画は同フレームの TrailPass。
REGISTER_RENDER_SYSTEM(TrailSystem, Priority::RenderStage::R08_Main);
