#include "TrailSystem.h"

#include "Engine/Graphics/Renderer/TrailRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;

// 断面列を滑らかにするための Catmull-Rom 補間。
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

// 折れ点番号から決定論的な擬似乱数(0〜1)を作る。
//
// 断面列は毎フレーム作り直されるため、乱数を保存すると模様が保てない。
// 番号から毎回引き直すことで、同じ区間は何フレーム後も同じ折れ方になる。
static float Hash01(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

// 1区間あたりの補間分割数。断面列の密度。
static constexpr int SUBDIVISIONS = 4;

// ===========================================================================
// 断面列の生成
// ===========================================================================
void TrailSystem::BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const
{
    outRings.clear();
    if (trail.count < 2)
        return;

    const int MAX = TrailComponent::MAX_SEGMENTS;

    auto getSegment = [&](int index) -> const TrailComponent::Segment & {
        index = std::clamp(index, 0, trail.count - 1);
        return trail.segments[(trail.head + index) % MAX];
    };

    outRings.reserve(static_cast<size_t>(trail.count - 1) * SUBDIVISIONS + 1);

    // =========================================================
    // 補間なし
    // =========================================================
    if (!trail.useInterpolation)
    {
        outRings.reserve(static_cast<size_t>(trail.count));

        for (int i = 0; i < trail.count; ++i)
        {
            const auto &segment = getSegment(i);

            const float invLife =
                (trail.lifeTime > 0.0f)
                    ? (1.0f / trail.lifeTime)
                    : 0.0f;

            const float life =
                std::clamp(1.0f - segment.age * invLife, 0.0f, 1.0f);

            const float taper =
                std::pow(life, trail.widthCurvePower);

            const float w = trail.width * taper;

            Ring ring;

            ring.center = {
                (segment.base.x + segment.tip.x) * 0.5f,
                (segment.base.y + segment.tip.y) * 0.5f,
                (segment.base.z + segment.tip.z) * 0.5f};

            ring.half = {
                (segment.tip.x - ring.center.x) * w,
                (segment.tip.y - ring.center.y) * w,
                (segment.tip.z - ring.center.z) * w};

            ring.age      = segment.age;
            ring.taper    = w;
            ring.segStamp = segment.stamp;
            ring.segT     = 0.0f;

            outRings.push_back(ring);
        }

        return;
    }

    // =========================================================
    // 補間あり（Catmull-Rom）
    // =========================================================

    const float invLife =
        (trail.lifeTime > 0.0f)
            ? (1.0f / trail.lifeTime)
            : 0.0f;

    outRings.reserve(
        static_cast<size_t>(trail.count - 1) * SUBDIVISIONS + 1);

    for (int i = 0; i < trail.count - 1; ++i)
    {
        const auto &s0 = getSegment(i - 1);
        const auto &s1 = getSegment(i);
        const auto &s2 = getSegment(i + 1);
        const auto &s3 = getSegment(i + 2);

        const int jEnd =
            (i == trail.count - 2)
                ? SUBDIVISIONS
                : SUBDIVISIONS - 1;

        for (int j = 0; j <= jEnd; ++j)
        {
            const float t =
                static_cast<float>(j) /
                static_cast<float>(SUBDIVISIONS);

            const XMFLOAT3 base =
                CatmullRom(s0.base, s1.base, s2.base, s3.base, t);

            const XMFLOAT3 tip =
                CatmullRom(s0.tip, s1.tip, s2.tip, s3.tip, t);

            const float age =
                std::lerp(s1.age, s2.age, t);

            const float life =
                std::clamp(1.0f - age * invLife, 0.0f, 1.0f);

            const float taper =
                std::pow(life, trail.widthCurvePower);

            const float w =
                trail.width * taper;

            Ring ring;

            ring.center = {
                (base.x + tip.x) * 0.5f,
                (base.y + tip.y) * 0.5f,
                (base.z + tip.z) * 0.5f};

            ring.half = {
                (tip.x - ring.center.x) * w,
                (tip.y - ring.center.y) * w,
                (tip.z - ring.center.z) * w};

            ring.age      = age;
            ring.taper    = w;
            ring.segStamp = s1.stamp;
            ring.segT     = t;

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

void TrailSystem::RingBasis(DirectX::FXMVECTOR       tangent,
                            const DirectX::XMFLOAT3 &half,
                            DirectX::XMVECTOR       &outU,
                            DirectX::XMVECTOR       &outV) const
{
    // 半ベクトルから接線成分を抜くと、断面内に収まる基準軸 U になる。
    // こうしておくと thickness=0 のとき筒が板と同じ太さで揃う。
    XMVECTOR u = XMLoadFloat3(&half);
    u          = XMVectorSubtract(u, XMVectorScale(tangent, XMVectorGetX(XMVector3Dot(u, tangent))));

    if (XMVectorGetX(XMVector3LengthSq(u)) < 1e-12f)
    {
        // 板の向きが進行方向と平行＝断面が潰れている。適当な直交軸で代用する。
        // 根本と先端をほぼ同じ点に置く使い方（足元の尾など）では常にこちらを通る。
        const XMVECTOR helper = (std::abs(XMVectorGetY(tangent)) > 0.99f)
                                    ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
                                    : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        u                     = XMVector3Cross(tangent, helper);
    }

    outU = XMVector3Normalize(u);
    outV = XMVector3Normalize(XMVector3Cross(tangent, outU));
}

float TrailSystem::RingRadius(const TrailComponent &trail, const Ring &ring) const
{
    // thickness=0 は「根本→先端の長さの半分」＝板と同じ太さ。
    if (trail.thickness > 0.0f)
        return trail.thickness * ring.taper;

    return XMVectorGetX(XMVector3Length(XMLoadFloat3(&ring.half)));
}

TrailSystem::VertexPair TrailSystem::MakeVertex(const TrailComponent    &trail,
                                                const DirectX::XMFLOAT3 &pos,
                                                float                    age,
                                                float                    intensity,
                                                float                    vCoord) const
{
    const float invLife = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;

    float fade = 1.0f - age * invLife; // 尾ほど薄い
    if (fade < 0.0f)
        fade = 0.0f;

    const float mask = fade * intensity;

    // U＝長さ方向(先端0→末尾1)＝age正規化。tiling を掛け、スクロールオフセットを加える。
    const float    uAlong = std::clamp(age * invLife, 0.0f, 1.0f);
    const XMFLOAT2 uv     = {
        uAlong * trail.uvTiling.x + trail.uvScrollOffset.x,
        vCoord * trail.uvTiling.y + trail.uvScrollOffset.y};

    VertexPair pair;
    pair.additive.position = pos;
    pair.additive.color    = XMFLOAT4(trail.color.x, trail.color.y, trail.color.z,
                                      trail.color.w * mask);
    pair.additive.uv       = uv;

    // 歪みは色ではなくパラメータを運ぶ（意味は TrailDistortPS.hlsl と対）。
    // 加算色のアルファ(trail.color.w)を掛けないのは、「見た目は薄いが強く歪む」
    // 陽炎のような使い方を潰さないため。歪みの強さは distortion だけで決める。
    pair.distort.position = pos;
    pair.distort.color    = XMFLOAT4(trail.distortion, trail.distortionChroma,
                                     trail.distortionNoise, mask);
    pair.distort.uv       = uv; // 歪みPSは未使用だが同レイアウトなので埋めておく
    return pair;
}

void TrailSystem::EmitSink::PushTriangle(const VertexPair &a,
                                         const VertexPair &b,
                                         const VertexPair &c) const
{
    additive->push_back(a.additive);
    additive->push_back(b.additive);
    additive->push_back(c.additive);

    if (!distort)
        return;

    distort->push_back(a.distort);
    distort->push_back(b.distort);
    distort->push_back(c.distort);
}

// ===========================================================================
// ジグザグ（稲妻状の折れ）
// ===========================================================================
DirectX::XMVECTOR TrailSystem::ZigZagOffset(const TrailComponent &trail,
                                            uint32_t              node,
                                            DirectX::FXMVECTOR    u,
                                            DirectX::FXMVECTOR    v) const
{
    const uint32_t key = node * 2654435761u + trail.zigZagSeed;

    const float ax = Hash01(key) * 2.0f - 1.0f;
    const float ay = Hash01(key ^ 0x85EBCA6Bu) * 2.0f - 1.0f;

    XMVECTOR dir = XMVectorAdd(XMVectorScale(u, ax), XMVectorScale(v, ay));
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-12f)
        dir = u;
    dir = XMVector3Normalize(dir);

    // 向きだけを乱数に任せ、長さには下限を持たせる。長さまで一様乱数にすると
    // ときどき折れ点がほぼ真っ直ぐになり、稲妻らしい鋭さが途切れるため。
    const float mag = trail.zigZagAmplitude * (0.45f + 0.55f * Hash01(key ^ 0xC2B2AE35u));
    return XMVectorScale(dir, mag);
}

void TrailSystem::ApplyZigZag(const TrailComponent &trail, std::vector<Ring> &rings) const
{
    if (rings.size() < 2 || trail.zigZagAmplitude <= 0.0f)
        return;

    const int   frequency = std::clamp(trail.zigZagFrequency, 1, 8);
    const float freqF     = static_cast<float>(frequency);

    // 折れは「滑らかな軌跡」を基準に決めるので、接線にはずらす前の中心が要る。
    // 先頭から順に書き換えるため、直前の1点だけ控えておけば足りる。
    XMFLOAT3 prevSmooth = rings[0].center;

    for (size_t i = 0; i < rings.size(); ++i)
    {
        const bool     hasNext = (i + 1 < rings.size());
        const XMVECTOR a       = hasNext ? XMLoadFloat3(&rings[i].center) : XMLoadFloat3(&prevSmooth);
        const XMVECTOR b       = hasNext ? XMLoadFloat3(&rings[i + 1].center) : XMLoadFloat3(&rings[i].center);

        XMVECTOR tangent = XMVectorSubtract(b, a);
        if (XMVectorGetX(XMVector3LengthSq(tangent)) < 1e-12f)
            tangent = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        tangent = XMVector3Normalize(tangent);

        prevSmooth = rings[i].center;

        XMVECTOR u, v;
        RingBasis(tangent, rings[i].half, u, v);

        // 折れ点は「区間の通し番号 × 折れ点数」で数える。区間ごとに +1 される
        // 番号なので、区間をまたいでも番号が途切れず、模様が空間に焼き付く。
        const float    phase = rings[i].segT * freqF;
        const float    step  = std::floor(phase);
        const uint32_t node  = rings[i].segStamp * static_cast<uint32_t>(frequency) + static_cast<uint32_t>(step);

        // 折れ点の間は直線で結ぶ。ここを曲線にすると角が丸まり、稲妻に見えない。
        const XMVECTOR offset = XMVectorLerp(ZigZagOffset(trail, node, u, v),
                                             ZigZagOffset(trail, node + 1, u, v),
                                             phase - step);

        // 先端側は刃（足元）に付いていなければならないので、直近の数断面は効かせない。
        // ここを外すと軌跡の根元が本体から浮いて、毎フレーム跳ねて見える。
        const float attach = std::clamp(
            static_cast<float>(rings.size() - 1 - i) / static_cast<float>(SUBDIVISIONS),
            0.0f,
            1.0f);

        XMStoreFloat3(&rings[i].center,
                      XMVectorAdd(XMLoadFloat3(&rings[i].center), XMVectorScale(offset, attach)));
    }
}

// ===========================================================================
// 形状ごとの三角形生成
// ===========================================================================
void TrailSystem::EmitRibbon(const TrailComponent    &trail,
                             const std::vector<Ring> &rings,
                             const EmitSink          &sink) const
{
    for (size_t i = 0; i + 1 < rings.size(); ++i)
    {
        const Ring &r0 = rings[i];
        const Ring &r1 = rings[i + 1];

        const XMFLOAT3 b0 = {r0.center.x - r0.half.x, r0.center.y - r0.half.y, r0.center.z - r0.half.z};
        const XMFLOAT3 t0 = {r0.center.x + r0.half.x, r0.center.y + r0.half.y, r0.center.z + r0.half.z};
        const XMFLOAT3 b1 = {r1.center.x - r1.half.x, r1.center.y - r1.half.y, r1.center.z - r1.half.z};
        const XMFLOAT3 t1 = {r1.center.x + r1.half.x, r1.center.y + r1.half.y, r1.center.z + r1.half.z};

        // V＝幅方向。根本(b)=0 / 先端(t)=1。
        const VertexPair vb0 = MakeVertex(trail, b0, r0.age, 1.0f, 0.0f);
        const VertexPair vt0 = MakeVertex(trail, t0, r0.age, 1.0f, 1.0f);
        const VertexPair vb1 = MakeVertex(trail, b1, r1.age, 1.0f, 0.0f);
        const VertexPair vt1 = MakeVertex(trail, t1, r1.age, 1.0f, 1.0f);

        sink.PushTriangle(vb0, vt0, vt1);
        sink.PushTriangle(vb0, vt1, vb1);
    }
}

void TrailSystem::EmitCross(const TrailComponent    &trail,
                            const std::vector<Ring> &rings,
                            const EmitSink          &sink) const
{
    // 1枚目は板と同じ。2枚目は進行方向まわりに 90 度回した面。
    // これだけで「どの角度から見ても必ずどちらかの面が正面を向く」ようになる。
    EmitRibbon(trail, rings, sink);

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

        // V＝幅方向。2枚目(十字面)も根本=0 / 先端=1 で揃える。
        const VertexPair vb0 = MakeVertex(trail, b0, r0.age, 1.0f, 0.0f);
        const VertexPair vt0 = MakeVertex(trail, t0, r0.age, 1.0f, 1.0f);
        const VertexPair vb1 = MakeVertex(trail, b1, r1.age, 1.0f, 0.0f);
        const VertexPair vt1 = MakeVertex(trail, t1, r1.age, 1.0f, 1.0f);

        sink.PushTriangle(vb0, vt0, vt1);
        sink.PushTriangle(vb0, vt1, vb1);
    }
}

void TrailSystem::EmitTube(const TrailComponent    &trail,
                           const std::vector<Ring> &rings,
                           const DirectX::XMFLOAT3 &cameraEye,
                           const EmitSink          &sink) const
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
        Frame f;
        RingBasis(RingTangent(rings, i), rings[i].half, f.u, f.v);
        f.radius = RingRadius(trail, rings[i]);
        frames.push_back(f);
    }

    const XMVECTOR eye = XMLoadFloat3(&cameraEye);
    const float    rim = std::clamp(trail.rimPower, 0.0f, 1.0f);

    // 断面 i の k 番目の頂点と、その明るさを求める。
    auto ringVertex = [&](size_t i, int k, XMFLOAT3 &outPos, float &outIntensity) {
        const float    angle = XM_2PI * static_cast<float>(k) / static_cast<float>(sides);
        const XMVECTOR dir =
            XMVectorAdd(XMVectorScale(frames[i].u, std::cos(angle)),
                        XMVectorScale(frames[i].v, std::sin(angle)));

        const XMVECTOR pos =
            XMVectorAdd(XMLoadFloat3(&rings[i].center), XMVectorScale(dir, frames[i].radius));
        XMStoreFloat3(&outPos, pos);

        // 視線に対して横を向く面ほど明るくする（＝輪郭が光る）。
        // 加算合成では全面が同じ明るさだと、筒ではなく のっぺりした塊に見えるため。
        const XMVECTOR view   = XMVector3Normalize(XMVectorSubtract(pos, eye));
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

            // V＝円周位置。継ぎ目を避けるため wrap 前の k, k+1 を使う。
            const float vK  = static_cast<float>(k) / static_cast<float>(sides);
            const float vK1 = static_cast<float>(k + 1) / static_cast<float>(sides);

            const VertexPair v00 = MakeVertex(trail, p00, rings[i].age, i00, vK);
            const VertexPair v01 = MakeVertex(trail, p01, rings[i].age, i01, vK1);
            const VertexPair v10 = MakeVertex(trail, p10, rings[i + 1].age, i10, vK);
            const VertexPair v11 = MakeVertex(trail, p11, rings[i + 1].age, i11, vK1);

            sink.PushTriangle(v00, v01, v11);
            sink.PushTriangle(v00, v11, v10);
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
    std::vector<TrailRenderer::Vertex> distortVerts;
    std::vector<Ring>                  rings;

    ForEach([&](const TransformComponent &transform, TrailComponent &trail) {
        // UVスクロールのオフセットを進める（桁あふれ防止で適当に wrap）。
        //   これは尾の形状ではなく模様の流れなので、実時間(描画dt)で進めてよい。
        //   高リフレッシュレートほど滑らかになるだけで、速度は環境によらず同じ。
        trail.uvScrollOffset.x = std::fmod(trail.uvScrollOffset.x + trail.uvScroll.x * dt, 1024.0f);
        trail.uvScrollOffset.y = std::fmod(trail.uvScrollOffset.y + trail.uvScroll.y * dt, 1024.0f);

        // ── 1. 現在の刃2端点をワールド空間へ（w=1 で並進込み変換） ──
        const XMMATRIX W     = XMLoadFloat4x4(&transform.renderWorldMatrix);
        const XMVECTOR baseW = XMVector3Transform(XMLoadFloat3(&trail.baseOffset), W);
        const XMVECTOR tipW  = XMVector3Transform(XMLoadFloat3(&trail.tipOffset), W);

        XMFLOAT3 baseF, tipF;
        XMStoreFloat3(&baseF, baseW);
        XMStoreFloat3(&tipF, tipW);

        const int MAX = TrailComponent::MAX_SEGMENTS;

        // ── 2. 区間の老化と寿命切れ破棄は TrailRecordSystem(ロジック相)が行う ──
        //   尾の長さを環境のリフレッシュレートから切り離すため、時間の進行は
        //   固定 dt 側に一本化してある。ここでは触らない。

        // ── 3. 新区間を刻むか判定 ──
        //   刻む「頻度」はロジック相が決める(sampleRequested)。刻む「位置」は補間済みの
        //   renderWorldMatrix、つまり実際に画面に見えている刃の位置で決める。
        //
        //   両方を描画相に置くと、ボーン姿勢が更新されないフレーム(90Hz なら3回に1回)で
        //   ルートの補間移動ぶんだけずれた断面が重複記録され、尾が二重の線になる。
        //   逆に両方をロジック相へ移すと、尾が補間中の刃から最大1ステップぶん離れる。
        //   頻度だけを固定するのが、どちらの破綻も踏まない置き方。
        const bool requested  = trail.sampleRequested;
        trail.sampleRequested = false; // 印は立てた側に関わらず必ずここで降ろす

        bool record = requested;
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
            trail.segments[writeIdx].base  = baseF;
            trail.segments[writeIdx].tip   = tipF;
            trail.segments[writeIdx].age   = 0.0f;
            trail.segments[writeIdx].stamp = trail.nextStamp++;
        }

        // ── 4. 区間列を断面列へ補間し、形状に応じた三角形を提出 ──
        if (trail.count < 2)
            return;

        BuildRings(trail, rings);
        if (rings.size() < 2)
            return;

        // 形状(板/十字/筒)を作る前に芯線を折る。こうすると3形状すべてが
        // 同じ稲妻をなぞり、形状ごとにジグザグを実装せずに済む。
        if (trail.useZigZag)
            ApplyZigZag(trail, rings);

        verts.clear();
        distortVerts.clear();

        EmitSink sink;
        sink.additive = &verts;
        if (trail.distortion > 0.0f)
            sink.distort = &distortVerts;

        switch (static_cast<TrailShape>(trail.shape))
        {
        case TrailShape::Cross:
            EmitCross(trail, rings, sink);
            break;
        case TrailShape::Tube:
            EmitTube(trail, rings, cameraEye, sink);
            break;
        case TrailShape::Ribbon:
        default:
            EmitRibbon(trail, rings, sink);
            break;
        }

        // テクスチャを引く（パスがあるときだけ）。ResourceManager はパスでキャッシュするので、
        // 毎フレーム呼んでも読み込むのは初回だけ。パスを変えれば新しいハンドルになる。
        ID3D11ShaderResourceView *trailSRV = nullptr;
        if (!trail.texturePath.empty())
        {
            trail.textureHandle = ResourceManager::Instance().LoadTexture(trail.texturePath.c_str());
            if (trail.textureHandle.IsValid())
                trailSRV = ResourceManager::Instance().GetTexture(trail.textureHandle);
        }

        renderer.SubmitTriangleList(verts.data(), verts.size(), trailSRV);
        renderer.SubmitDistortTriangleList(distortVerts.data(), distortVerts.size());
    });
}

// 実際の描画は同じフレームの TrailPass が行う
REGISTER_RENDER_SYSTEM(TrailSystem, Priority::RenderStage::R08_Main);
