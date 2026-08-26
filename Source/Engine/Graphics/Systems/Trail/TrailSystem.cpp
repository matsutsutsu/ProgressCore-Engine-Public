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
//   - 「区間の補間(BuildRings)」と「形状の生成(Emit*)」を分けている。板1枚では面が
//     視線と平行になった瞬間に線へ潰れるため、同じ断面列から十字・筒も作れるようにした。
// ===========================================================================
#include "TrailSystem.h"

#include "Engine/Graphics/Renderer/TrailRenderer.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;

/// @brief 断面列を滑らかにするための Catmull-Rom 補間。
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

/// @brief 1区間あたりの補間分割数。断面列の密度。
static constexpr int SUBDIVISIONS = 4;

// ===========================================================================
// 断面列の生成
// ===========================================================================
void TrailSystem::BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const
{
    outRings.clear();
    if (trail.count < 2)
        return;

    const int   MAX     = TrailComponent::MAX_SEGMENTS;
    const float invLife = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;

    auto getSegment = [&](int index) -> const TrailComponent::Segment & {
        index = std::clamp(index, 0, trail.count - 1);
        return trail.segments[(trail.head + index) % MAX];
    };

    outRings.reserve(static_cast<size_t>(trail.count - 1) * SUBDIVISIONS + 1);

    for (int i = 0; i < trail.count - 1; ++i)
    {
        const auto &s0 = getSegment(i - 1);
        const auto &s1 = getSegment(i);
        const auto &s2 = getSegment(i + 1);
        const auto &s3 = getSegment(i + 2);

        // 最後の区間だけ終端(t=1)も出す。断面列を継ぎ目のない1本の列にするため。
        const int jEnd = (i == trail.count - 2) ? SUBDIVISIONS : SUBDIVISIONS - 1;

        for (int j = 0; j <= jEnd; ++j)
        {
            const float t = static_cast<float>(j) / static_cast<float>(SUBDIVISIONS);

            const XMFLOAT3 base = CatmullRom(s0.base, s1.base, s2.base, s3.base, t);
            const XMFLOAT3 tip  = CatmullRom(s0.tip, s1.tip, s2.tip, s3.tip, t);

            const float age  = std::lerp(s1.age, s2.age, t);
            const float life = std::clamp(1.0f - age * invLife, 0.0f, 1.0f);

            // 新しい部分ほど太く、古い部分ほど細くする。
            const float taper = std::pow(life, trail.widthCurvePower);
            const float w     = trail.width * taper;

            Ring ring;
            ring.center = {(base.x + tip.x) * 0.5f,
                (base.y + tip.y) * 0.5f,
                (base.z + tip.z) * 0.5f};
            ring.half  = {(tip.x - ring.center.x) * w,
                (tip.y - ring.center.y) * w,
                (tip.z - ring.center.z) * w};
            ring.age   = age;
            ring.taper = w;

            outRings.push_back(ring);
        }
    }
}

// ===========================================================================
// 断面の補助
// ===========================================================================
DirectX::XMVECTOR TrailSystem::RingTangent(const std::vector<Ring> &rings, size_t i) const
{
    // 末尾の断面は「次」が無いので直前の向きを流用する。
    const size_t a = (i + 1 < rings.size()) ? i : (i - 1);
    const size_t b = a + 1;

    XMVECTOR dir = XMVectorSubtract(XMLoadFloat3(&rings[b].center), XMLoadFloat3(&rings[a].center));

    // 静止直後などで前後の断面が重なると 0 ベクトルになる。適当な軸へ逃がす。
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-12f)
        dir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    return XMVector3Normalize(dir);
}

float TrailSystem::RingRadius(const TrailComponent &trail, const Ring &ring) const
{
    // thickness=0 は「根本→先端の長さの半分」＝板と同じ太さ。
    if (trail.thickness > 0.0f)
        return trail.thickness * ring.taper;

    return XMVectorGetX(XMVector3Length(XMLoadFloat3(&ring.half)));
}

TrailRenderer::Vertex TrailSystem::MakeVertex(const TrailComponent &trail,
    const DirectX::XMFLOAT3                                        &pos,
    float                                                           age,
    float                                                           intensity) const
{
    const float invLife = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;

    float fade = 1.0f - age * invLife; // 尾ほど薄い
    if (fade < 0.0f)
        fade = 0.0f;

    TrailRenderer::Vertex v;
    v.position = pos;
    v.color    = XMFLOAT4(trail.color.x, trail.color.y, trail.color.z,
        trail.color.w * fade * intensity);
    return v;
}

// ===========================================================================
// 形状ごとの三角形生成
// ===========================================================================
void TrailSystem::EmitRibbon(const TrailComponent &trail,
    const std::vector<Ring>                       &rings,
    std::vector<TrailRenderer::Vertex>            &outVerts) const
{
    for (size_t i = 0; i + 1 < rings.size(); ++i)
    {
        const Ring &r0 = rings[i];
        const Ring &r1 = rings[i + 1];

        const XMFLOAT3 b0 = {r0.center.x - r0.half.x, r0.center.y - r0.half.y, r0.center.z - r0.half.z};
        const XMFLOAT3 t0 = {r0.center.x + r0.half.x, r0.center.y + r0.half.y, r0.center.z + r0.half.z};
        const XMFLOAT3 b1 = {r1.center.x - r1.half.x, r1.center.y - r1.half.y, r1.center.z - r1.half.z};
        const XMFLOAT3 t1 = {r1.center.x + r1.half.x, r1.center.y + r1.half.y, r1.center.z + r1.half.z};

        const TrailRenderer::Vertex vb0 = MakeVertex(trail, b0, r0.age, 1.0f);
        const TrailRenderer::Vertex vt0 = MakeVertex(trail, t0, r0.age, 1.0f);
        const TrailRenderer::Vertex vb1 = MakeVertex(trail, b1, r1.age, 1.0f);
        const TrailRenderer::Vertex vt1 = MakeVertex(trail, t1, r1.age, 1.0f);

        outVerts.push_back(vb0);
        outVerts.push_back(vt0);
        outVerts.push_back(vt1);

        outVerts.push_back(vb0);
        outVerts.push_back(vt1);
        outVerts.push_back(vb1);
    }
}

void TrailSystem::EmitCross(const TrailComponent &trail,
    const std::vector<Ring>                      &rings,
    std::vector<TrailRenderer::Vertex>           &outVerts) const
{
    // 1枚目は板と同じ。2枚目は進行方向まわりに 90 度回した面。
    // これだけで「どの角度から見ても必ずどちらかの面が正面を向く」ようになる。
    EmitRibbon(trail, rings, outVerts);

    for (size_t i = 0; i + 1 < rings.size(); ++i)
    {
        const Ring &r0 = rings[i];
        const Ring &r1 = rings[i + 1];

        // 進行方向を軸に半ベクトルを 90 度回す（軸に垂直な成分だけを回せばよい）。
        auto rotated = [&](const Ring &r, size_t index) -> XMVECTOR {
            const XMVECTOR axis = RingTangent(rings, index);
            const XMVECTOR half = XMLoadFloat3(&r.half);
            const XMVECTOR perp = XMVector3Cross(axis, half);

            // 軸と半ベクトルが平行だと外積が 0 になる。その場合は回しても意味が無いので
            // 板と同じ面を返し、二重描画で明るくなるだけに留める。
            if (XMVectorGetX(XMVector3LengthSq(perp)) < 1e-12f)
                return half;

            const float radius = RingRadius(trail, r);
            return XMVectorScale(XMVector3Normalize(perp), radius);
        };

        const XMVECTOR h0 = rotated(r0, i);
        const XMVECTOR h1 = rotated(r1, i + 1);

        XMFLOAT3 b0, t0, b1, t1;
        XMStoreFloat3(&b0, XMVectorSubtract(XMLoadFloat3(&r0.center), h0));
        XMStoreFloat3(&t0, XMVectorAdd(XMLoadFloat3(&r0.center), h0));
        XMStoreFloat3(&b1, XMVectorSubtract(XMLoadFloat3(&r1.center), h1));
        XMStoreFloat3(&t1, XMVectorAdd(XMLoadFloat3(&r1.center), h1));

        const TrailRenderer::Vertex vb0 = MakeVertex(trail, b0, r0.age, 1.0f);
        const TrailRenderer::Vertex vt0 = MakeVertex(trail, t0, r0.age, 1.0f);
        const TrailRenderer::Vertex vb1 = MakeVertex(trail, b1, r1.age, 1.0f);
        const TrailRenderer::Vertex vt1 = MakeVertex(trail, t1, r1.age, 1.0f);

        outVerts.push_back(vb0);
        outVerts.push_back(vt0);
        outVerts.push_back(vt1);

        outVerts.push_back(vb0);
        outVerts.push_back(vt1);
        outVerts.push_back(vb1);
    }
}

void TrailSystem::EmitTube(const TrailComponent &trail,
    const std::vector<Ring>                     &rings,
    const DirectX::XMFLOAT3                     &cameraEye,
    std::vector<TrailRenderer::Vertex>          &outVerts) const
{
    const int sides = std::clamp(trail.tubeSides, 3, 16);

    // 断面ごとの正規直交フレーム。U は板の向き（＝根本→先端）を基準にするので、
    // thickness=0 のときは板と同じ太さの筒になる。
    struct Frame
    {
        XMVECTOR u;
        XMVECTOR v;
        float    radius;
    };

    std::vector<Frame> frames;
    frames.reserve(rings.size());

    for (size_t i = 0; i < rings.size(); ++i)
    {
        const XMVECTOR tangent = RingTangent(rings, i);

        // 半ベクトルから接線成分を抜いて、断面内の基準軸 U を作る。
        XMVECTOR u = XMLoadFloat3(&rings[i].half);
        u          = XMVectorSubtract(u, XMVectorScale(tangent, XMVectorGetX(XMVector3Dot(u, tangent))));

        if (XMVectorGetX(XMVector3LengthSq(u)) < 1e-12f)
        {
            // 板の向きが進行方向と平行＝断面が潰れている。適当な直交軸で代用する。
            const XMVECTOR helper = (std::abs(XMVectorGetY(tangent)) > 0.99f)
                                        ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
                                        : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            u = XMVector3Cross(tangent, helper);
        }

        u = XMVector3Normalize(u);

        Frame f;
        f.u      = u;
        f.v      = XMVector3Normalize(XMVector3Cross(tangent, u));
        f.radius = RingRadius(trail, rings[i]);
        frames.push_back(f);
    }

    const XMVECTOR eye = XMLoadFloat3(&cameraEye);
    const float    rim = std::clamp(trail.rimPower, 0.0f, 1.0f);

    // 断面 i の k 番目の頂点と、その明るさを求める。
    auto ringVertex = [&](size_t i, int k, XMFLOAT3 &outPos, float &outIntensity) {
        const float angle = XM_2PI * static_cast<float>(k) / static_cast<float>(sides);
        const XMVECTOR dir =
            XMVectorAdd(XMVectorScale(frames[i].u, std::cos(angle)),
                XMVectorScale(frames[i].v, std::sin(angle)));

        const XMVECTOR pos =
            XMVectorAdd(XMLoadFloat3(&rings[i].center), XMVectorScale(dir, frames[i].radius));
        XMStoreFloat3(&outPos, pos);

        // 視線に対して横を向く面ほど明るくする（＝輪郭が光る）。
        // 加算合成では全面が同じ明るさだと、筒ではなく のっぺりした塊に見えるため。
        const XMVECTOR view  = XMVector3Normalize(XMVectorSubtract(pos, eye));
        const float    facing = std::abs(XMVectorGetX(XMVector3Dot(dir, view)));
        outIntensity          = 1.0f - rim * facing;
    };

    for (size_t i = 0; i + 1 < rings.size(); ++i)
    {
        for (int k = 0; k < sides; ++k)
        {
            const int k1 = (k + 1) % sides;

            XMFLOAT3 p00, p01, p10, p11;
            float    i00, i01, i10, i11;
            ringVertex(i, k, p00, i00);
            ringVertex(i, k1, p01, i01);
            ringVertex(i + 1, k, p10, i10);
            ringVertex(i + 1, k1, p11, i11);

            const TrailRenderer::Vertex v00 = MakeVertex(trail, p00, rings[i].age, i00);
            const TrailRenderer::Vertex v01 = MakeVertex(trail, p01, rings[i].age, i01);
            const TrailRenderer::Vertex v10 = MakeVertex(trail, p10, rings[i + 1].age, i10);
            const TrailRenderer::Vertex v11 = MakeVertex(trail, p11, rings[i + 1].age, i11);

            outVerts.push_back(v00);
            outVerts.push_back(v01);
            outVerts.push_back(v11);

            outVerts.push_back(v00);
            outVerts.push_back(v11);
            outVerts.push_back(v10);
        }
    }
}

// ===========================================================================
// 更新
// ===========================================================================
void TrailSystem::Update(float dt)
{
    auto &renderer = TrailRenderer::Instance();

    // Tube のシルエット強調に使う視点。カメラが無いシーンでは原点扱いで害は無い。
    XMFLOAT3 cameraEye = {0.0f, 0.0f, 0.0f};
    if (m_world->HasResource<Camera *>())
    {
        if (Camera *cam = m_world->GetResource<Camera *>())
            cameraEye = cam->GetEye();
    }

    // 頂点と断面列の一時バッファ（エンティティ間で使い回してアロケーションを抑える）。
    // ForEach はシングルスレッド版のため関数ローカルで安全。
    std::vector<TrailRenderer::Vertex> verts;
    std::vector<Ring>                  rings;

    ForEach([&](const TransformComponent &transform, TrailComponent &trail) {
        // ── 1. 現在の刃2端点をワールド空間へ（w=1 で並進込み変換） ──
        const XMMATRIX W     = XMLoadFloat4x4(&transform.renderWorldMatrix);
        const XMVECTOR baseW = XMVector3Transform(XMLoadFloat3(&trail.baseOffset), W);
        const XMVECTOR tipW  = XMVector3Transform(XMLoadFloat3(&trail.tipOffset), W);

        XMFLOAT3 baseF, tipF;
        XMStoreFloat3(&baseF, baseW);
        XMStoreFloat3(&tipF, tipW);

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
            int      newest  = (trail.head + trail.count - 1) % MAX;
            XMVECTOR lastTip = XMLoadFloat3(&trail.segments[newest].tip);
            float    movedSq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(tipW, lastTip)));
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

        // ── 4. 区間列を断面列へ補間し、形状に応じた三角形を提出 ──
        if (trail.count < 2)
            return;

        BuildRings(trail, rings);
        if (rings.size() < 2)
            return;

        verts.clear();

        switch (static_cast<TrailShape>(trail.shape))
        {
        case TrailShape::Cross:
            EmitCross(trail, rings, verts);
            break;
        case TrailShape::Tube:
            EmitTube(trail, rings, cameraEye, verts);
            break;
        case TrailShape::Ribbon:
        default:
            EmitRibbon(trail, rings, verts);
            break;
        }

        renderer.SubmitTriangleList(verts.data(), verts.size());
    });
}

// メイン描画ステージで区間更新＆頂点提出。実際の描画は同フレームの TrailPass。
REGISTER_RENDER_SYSTEM(TrailSystem, Priority::RenderStage::R08_Main);
