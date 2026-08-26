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

static DirectX::XMFLOAT3 CatmullRom(
    const DirectX::XMFLOAT3 &p0,
    const DirectX::XMFLOAT3 &p1,
    const DirectX::XMFLOAT3 &p2,
    const DirectX::XMFLOAT3 &p3,
    float                    t)
{
    using namespace DirectX;

    const XMVECTOR v0 = XMLoadFloat3(&p0);
    const XMVECTOR v1 = XMLoadFloat3(&p1);
    const XMVECTOR v2 = XMLoadFloat3(&p2);
    const XMVECTOR v3 = XMLoadFloat3(&p3);

    const XMVECTOR result =
        0.5f *
        (2.0f * v1 +
            (-v0 + v2) * t +
            (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * (t * t) +
            (-v0 + 3.0f * v1 - 3.0f * v2 + v3) * (t * t * t));

    XMFLOAT3 out;
    XMStoreFloat3(&out, result);
    return out;
}

auto applyWidth = [](const XMFLOAT3 &base, const XMFLOAT3 &tip, float width) {
    XMFLOAT3 center;
    center.x = (base.x + tip.x) * 0.5f;
    center.y = (base.y + tip.y) * 0.5f;
    center.z = (base.z + tip.z) * 0.5f;

    XMFLOAT3 resultBase;
    XMFLOAT3 resultTip;

    resultBase.x = center.x + (base.x - center.x) * width;
    resultBase.y = center.y + (base.y - center.y) * width;
    resultBase.z = center.z + (base.z - center.z) * width;

    resultTip.x = center.x + (tip.x - center.x) * width;
    resultTip.y = center.y + (tip.y - center.y) * width;
    resultTip.z = center.z + (tip.z - center.z) * width;

    return std::pair<XMFLOAT3, XMFLOAT3>(resultBase, resultTip);
};



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

        constexpr int SUBDIVISIONS = 4;

        auto getSegment = [&](int index) -> const TrailComponent::Segment & {
            index = std::clamp(index, 0, trail.count - 1);
            return trail.segments[(trail.head + index) % MAX];
        };

        for (int i = 0; i < trail.count - 1; ++i)
        {
            const auto &s0 = getSegment(i - 1);
            const auto &s1 = getSegment(i);
            const auto &s2 = getSegment(i + 1);
            const auto &s3 = getSegment(i + 2);

            for (int j = 0; j < SUBDIVISIONS; ++j)
            {
                const float t0 =
                    static_cast<float>(j) /
                    static_cast<float>(SUBDIVISIONS);

                const float t1 =
                    static_cast<float>(j + 1) /
                    static_cast<float>(SUBDIVISIONS);

                XMFLOAT3 base0 =
                    CatmullRom(s0.base, s1.base, s2.base, s3.base, t0);

                XMFLOAT3 tip0 =
                    CatmullRom(s0.tip, s1.tip, s2.tip, s3.tip, t0);

                XMFLOAT3 base1 =
                    CatmullRom(s0.base, s1.base, s2.base, s3.base, t1);

                XMFLOAT3 tip1 =
                    CatmullRom(s0.tip, s1.tip, s2.tip, s3.tip, t1);

                const float age0 =
                    std::lerp(s1.age, s2.age, t0);

                const float age1 =
                    std::lerp(s1.age, s2.age, t1);

                // 0 = 古い尾、1 = 新しい部分
                const float life0 =
                    std::clamp(1.0f - age0 * invLife, 0.0f, 1.0f);

                const float life1 =
                    std::clamp(1.0f - age1 * invLife, 0.0f, 1.0f);

                // 新しい部分ほど太く、古い部分ほど細くする。
                const float width0 =
                    trail.width *
                    std::pow(life0, trail.widthCurvePower);

                const float width1 =
                    trail.width *
                    std::pow(life1, trail.widthCurvePower);

                auto [scaledBase0, scaledTip0] =
                    applyWidth(base0, tip0, width0);

                auto [scaledBase1, scaledTip1] =
                    applyWidth(base1, tip1, width1);

                TrailRenderer::Vertex b0 =
                    makeVert(scaledBase0, age0);

                TrailRenderer::Vertex t0v =
                    makeVert(scaledTip0, age0);

                TrailRenderer::Vertex b1 =
                    makeVert(scaledBase1, age1);

                TrailRenderer::Vertex t1v =
                    makeVert(scaledTip1, age1);

                verts.push_back(b0);
                verts.push_back(t0v);
                verts.push_back(t1v);

                verts.push_back(b0);
                verts.push_back(t1v);
                verts.push_back(b1);
            }
        }



        renderer.SubmitTriangleList(verts.data(), verts.size());
    });
}

// メイン描画ステージで区間更新＆頂点提出。実際の描画は同フレームの TrailPass。
REGISTER_RENDER_SYSTEM(TrailSystem, Priority::RenderStage::R08_Main);
