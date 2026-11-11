#include "TrailSystem.h"

#include "Engine/Graphics/Renderer/TrailRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;

// 1区間あたりの補間分割数。断面列の密度
static constexpr int SUBDIVISIONS = 4;

// ---------------------------------------------------------------------------
// 断面の記録
// ---------------------------------------------------------------------------

void TrailSystem::RecordSegment(TrailComponent &trail, const DirectX::XMFLOAT3 &base, const DirectX::XMFLOAT3 &tip) const
{
    const int MAX = TrailComponent::MAX_SEGMENTS;

    int writeIdx;
    if (trail.count < MAX)
    {
        writeIdx = (trail.head + trail.count) % MAX;
        trail.count++;
    }
    else
    {
        // 満杯なので、一番古い断面を捨てて上書きする
        writeIdx   = trail.head;
        trail.head = (trail.head + 1) % MAX;
    }

    trail.segments[writeIdx].base = base;
    trail.segments[writeIdx].tip  = tip;
    trail.segments[writeIdx].age  = 0.0f;
}



// ---------------------------------------------------------------------------
// 断面列の生成
// ---------------------------------------------------------------------------

void TrailSystem::BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const
{
    outRings.clear();
    if (trail.count < 2)
    {
        return;
    }

    const int   MAX     = TrailComponent::MAX_SEGMENTS;
    const float invLife = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;

    auto getSegment = [&](int index) -> const TrailComponent::Segment & {
        index = std::clamp(index, 0, trail.count - 1);
        return trail.segments[(trail.head + index) % MAX];
    };

    // --- 補間なし ---
    if (!trail.useInterpolation)
    {
        for (int i = 0; i < trail.count; ++i)
        {
            const auto &segment = getSegment(i);
            outRings.push_back(MakeRing(trail, segment.base, segment.tip, segment.age, invLife));
        }

        return;
    }

    // --- 補間あり（Catmull-Rom） ---
    for (int i = 0; i < trail.count - 1; ++i)
    {
        const auto &s0 = getSegment(i - 1);
        const auto &s1 = getSegment(i);
        const auto &s2 = getSegment(i + 1);
        const auto &s3 = getSegment(i + 2);

        // 区間の終点（t=1）は次の区間の始点と同じなので、最後の区間でだけ含める
        const int jEnd = (i == trail.count - 2) ? SUBDIVISIONS : SUBDIVISIONS - 1;

        for (int j = 0; j <= jEnd; ++j)
        {
            const float t = static_cast<float>(j) / static_cast<float>(SUBDIVISIONS);

            const XMFLOAT3 base = CatmullRom(s0.base, s1.base, s2.base, s3.base, t);
            const XMFLOAT3 tip  = CatmullRom(s0.tip, s1.tip, s2.tip, s3.tip, t);
            const float    age  = std::lerp(s1.age, s2.age, t);

            outRings.push_back(MakeRing(trail, base, tip, age, invLife));
        }
    }
}



TrailSystem::Ring TrailSystem::MakeRing(const TrailComponent    &trail,
                                        const DirectX::XMFLOAT3 &base,
                                        const DirectX::XMFLOAT3 &tip,
                                        float                    age,
                                        float                    invLife) const
{
    const float life  = std::clamp(1.0f - age * invLife, 0.0f, 1.0f);
    const float taper = std::pow(life, trail.widthCurvePower);
    const float w     = trail.width * taper;

    Ring ring;

    ring.center = {
        (base.x + tip.x) * 0.5f,
        (base.y + tip.y) * 0.5f,
        (base.z + tip.z) * 0.5f};

    ring.half = {
        (tip.x - ring.center.x) * w,
        (tip.y - ring.center.y) * w,
        (tip.z - ring.center.z) * w};

    ring.age   = age;
    ring.taper = w;

    return ring;
}



DirectX::XMFLOAT3 TrailSystem::CatmullRom(const DirectX::XMFLOAT3 &p0,
                                          const DirectX::XMFLOAT3 &p1,
                                          const DirectX::XMFLOAT3 &p2,
                                          const DirectX::XMFLOAT3 &p3,
                                          float                    t) const
{
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



// ---------------------------------------------------------------------------
// 断面の補助
// ---------------------------------------------------------------------------

DirectX::XMVECTOR TrailSystem::RingTangent(const std::vector<Ring> &rings, size_t i) const
{
    // 末尾の断面は「次」がないので直前の向きを流用する
    const size_t a = (i + 1 < rings.size()) ? i : (i - 1);
    const size_t b = a + 1;

    XMVECTOR dir = XMVectorSubtract(XMLoadFloat3(&rings[b].center), XMLoadFloat3(&rings[a].center));

    // 静止直後などで前後の断面が重なると 0 ベクトルになる。適当な軸へ逃がす
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-12f)
    {
        dir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }

    return XMVector3Normalize(dir);
}



void TrailSystem::RingBasis(DirectX::FXMVECTOR       tangent,
                            const DirectX::XMFLOAT3 &half,
                            DirectX::XMVECTOR       &outU,
                            DirectX::XMVECTOR       &outV) const
{
    // 半ベクトルから接線成分を抜くと、断面内に収まる基準軸 U になる
    // こうしておくと thickness=0 のとき筒が板と同じ太さで揃う
    XMVECTOR u = XMLoadFloat3(&half);
    u          = XMVectorSubtract(u, XMVectorScale(tangent, XMVectorGetX(XMVector3Dot(u, tangent))));

    if (XMVectorGetX(XMVector3LengthSq(u)) < 1e-12f)
    {
        // 板の向きが進行方向と平行＝断面が潰れている。適当な直交軸で代用する
        // 根本と先端をほぼ同じ点に置く使い方（足元の尾など）では常にこちらを通る
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
    // thickness=0 は「根本→先端の長さの半分」＝板と同じ太さ
    if (trail.thickness > 0.0f)
    {
        return trail.thickness * ring.taper;
    }

    return XMVectorGetX(XMVector3Length(XMLoadFloat3(&ring.half)));
}



TrailSystem::VertexPair TrailSystem::MakeVertex(const TrailComponent    &trail,
                                                const DirectX::XMFLOAT3 &pos,
                                                float                    age,
                                                float                    intensity,
                                                float                    vCoord) const
{
    const float invLife = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;

    // 尾ほど薄い
    const float fade = (std::max)(1.0f - age * invLife, 0.0f);
    const float mask = fade * intensity;

    // U＝長さ方向(先端0→末尾1)＝age正規化。tiling を掛け、スクロールオフセットを加える
    const float    uAlong = std::clamp(age * invLife, 0.0f, 1.0f);
    const XMFLOAT2 uv     = {
        uAlong * trail.uvTiling.x + trail.uvScrollOffset.x,
        vCoord * trail.uvTiling.y + trail.uvScrollOffset.y};

    VertexPair pair;
    pair.additive.position = pos;
    pair.additive.color    = XMFLOAT4(trail.color.x, trail.color.y, trail.color.z,
                                      trail.color.w * mask);
    pair.additive.uv       = uv;

    // 歪みは色ではなくパラメータを運ぶ（意味は TrailDistortPS.hlsl と対）
    // 加算色のアルファ(trail.color.w)を掛けないのは、「見た目は薄いが強く歪む」
    // 陽炎のような使い方を潰さないため。歪みの強さは distortion だけで決める
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
    {
        return;
    }

    distort->push_back(a.distort);
    distort->push_back(b.distort);
    distort->push_back(c.distort);
}



// ---------------------------------------------------------------------------
// 形状ごとの三角形生成
// ---------------------------------------------------------------------------

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

        // V＝幅方向。根本(b)=0 / 先端(t)=1
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
    // 1枚目は板と同じ。2枚目は進行方向まわりに 90 度回した面
    // これだけで「どの角度から見ても必ずどちらかの面が正面を向く」ようになる
    EmitRibbon(trail, rings, sink);

    // 進行方向を軸に半ベクトルを 90 度回す（軸に垂直な成分だけを回せばよい）
    auto rotated = [&](const Ring &r, size_t index) -> XMVECTOR {
        const XMVECTOR axis = RingTangent(rings, index);
        const XMVECTOR half = XMLoadFloat3(&r.half);
        const XMVECTOR perp = XMVector3Cross(axis, half);

        // 軸と半ベクトルが平行だと外積が 0 になる。その場合は回しても意味がないので
        // 板と同じ面を返し、二重描画で明るくなるだけに留める
        if (XMVectorGetX(XMVector3LengthSq(perp)) < 1e-12f)
        {
            return half;
        }

        const float radius = RingRadius(trail, r);
        return XMVectorScale(XMVector3Normalize(perp), radius);
    };

    for (size_t i = 0; i + 1 < rings.size(); ++i)
    {
        const Ring &r0 = rings[i];
        const Ring &r1 = rings[i + 1];

        const XMVECTOR h0 = rotated(r0, i);
        const XMVECTOR h1 = rotated(r1, i + 1);

        XMFLOAT3 b0, t0, b1, t1;
        XMStoreFloat3(&b0, XMVectorSubtract(XMLoadFloat3(&r0.center), h0));
        XMStoreFloat3(&t0, XMVectorAdd(XMLoadFloat3(&r0.center), h0));
        XMStoreFloat3(&b1, XMVectorSubtract(XMLoadFloat3(&r1.center), h1));
        XMStoreFloat3(&t1, XMVectorAdd(XMLoadFloat3(&r1.center), h1));

        // V＝幅方向。2枚目(十字面)も根本=0 / 先端=1 で揃える
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
                           const EmitSink          &sink)
{
    const int sides = std::clamp(trail.tubeSides, 3, 16);

    // 断面ごとの正規直交フレーム。U は板の向き（＝根本→先端）を基準にするので、
    // thickness=0 のときは板と同じ太さの筒になる
    m_tubeFrames.clear();

    for (size_t i = 0; i < rings.size(); ++i)
    {
        TubeFrame f;
        RingBasis(RingTangent(rings, i), rings[i].half, f.u, f.v);
        f.radius = RingRadius(trail, rings[i]);
        m_tubeFrames.push_back(f);
    }

    const XMVECTOR eye = XMLoadFloat3(&cameraEye);
    const float    rim = std::clamp(trail.rimPower, 0.0f, 1.0f);

    // 断面 i の k 番目の頂点と、その明るさを求める
    auto ringVertex = [&](size_t i, int k, XMFLOAT3 &outPos, float &outIntensity) {
        const TubeFrame &frame = m_tubeFrames[i];

        const float    angle = XM_2PI * static_cast<float>(k) / static_cast<float>(sides);
        const XMVECTOR dir =
            XMVectorAdd(XMVectorScale(frame.u, std::cos(angle)),
                        XMVectorScale(frame.v, std::sin(angle)));

        const XMVECTOR pos =
            XMVectorAdd(XMLoadFloat3(&rings[i].center), XMVectorScale(dir, frame.radius));
        XMStoreFloat3(&outPos, pos);

        // 視線に対して横を向く面ほど明るくする（＝輪郭が光る）
        // 加算合成では全面が同じ明るさだと、筒ではなく のっぺりした塊に見えるため
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

            // V＝円周位置。継ぎ目を避けるため wrap 前の k, k+1 を使う
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



// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------

void TrailSystem::Update(float dt)
{
    if (!m_world->HasResource<TrailRenderer *>())
    {
        return;
    }

    TrailRenderer *renderer = m_world->GetResource<TrailRenderer *>();
    if (!renderer)
    {
        return;
    }

    // Tube のシルエット強調に使う視点。カメラがないシーンでは原点扱いで害はない
    XMFLOAT3 cameraEye = {0.0f, 0.0f, 0.0f};
    if (m_world->HasResource<Camera *>())
    {
        if (Camera *cam = m_world->GetResource<Camera *>())
        {
            cameraEye = cam->GetEye();
        }
    }

    // 描画ステージはポーズ中も走るので、一時停止中は模様の流れも止める
    const float presentDt = Time::PresentationDt(*m_world, dt);

    ForEach([&](const TransformComponent &transform, TrailComponent &trail) {
        // 処理フロー:
        // 1. UV スクロールを進める
        // 2. 今の刃の2端点をワールド空間へ変換する
        // 3. 新しい断面を刻むか決める
        // 4. 断面列を作り、形に応じた三角形を積む

        // ---------------------------------------------------------
        // 1. UV スクロール
        // ---------------------------------------------------------

        // 尾の形ではなく模様の流れなので、描画 dt で進めてよい
        // 高リフレッシュレートほど滑らかになるだけで、速度は環境によらず同じ

        // timeScale を掛けるのは、スロー中に尾の老化だけ遅れて模様が等速で流れると、
        // 尾だけが時間の流れから浮いて見えるため
        const float scrollDt = presentDt * trail.timeScale;

        // 桁あふれを防ぐため、適当な値で巻き戻す
        trail.uvScrollOffset.x = std::fmod(trail.uvScrollOffset.x + trail.uvScroll.x * scrollDt, 1024.0f);
        trail.uvScrollOffset.y = std::fmod(trail.uvScrollOffset.y + trail.uvScroll.y * scrollDt, 1024.0f);

        // ---------------------------------------------------------
        // 2. 刃の2端点
        // ---------------------------------------------------------

        // w=1 で並進込みの変換
        const XMMATRIX W     = XMLoadFloat4x4(&transform.renderWorldMatrix);
        const XMVECTOR baseW = XMVector3Transform(XMLoadFloat3(&trail.baseOffset), W);
        const XMVECTOR tipW  = XMVector3Transform(XMLoadFloat3(&trail.tipOffset), W);

        XMFLOAT3 baseF, tipF;
        XMStoreFloat3(&baseF, baseW);
        XMStoreFloat3(&tipF, tipW);

        // ---------------------------------------------------------
        // 3. 新しい断面を刻むか
        // ---------------------------------------------------------

        // 区間の老化と寿命切れの破棄は TrailRecordSystem（ロジック）が行う
        // 尾の長さを環境のリフレッシュレートから切り離すため、時間の進行は固定 dt 側に一本化してある

        // 刻む頻度はロジックが決め（sampleRequested）、刻む位置は画面に見えている刃の位置（renderWorldMatrix）で決める
        // 両方を描画側にすると尾が二重の線になり、両方をロジックにすると尾が刃から離れる
        const bool requested  = trail.sampleRequested;
        trail.sampleRequested = false; 

        bool record = requested;
        if (record && trail.count > 0)
        {
            const int      newest  = (trail.head + trail.count - 1) % TrailComponent::MAX_SEGMENTS;
            const XMVECTOR lastTip = XMLoadFloat3(&trail.segments[newest].tip);
            const float    movedSq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(tipW, lastTip)));

            if (movedSq < trail.minDistance * trail.minDistance)
            {
                record = false;
            }
        }

        if (record)
        {
            RecordSegment(trail, baseF, tipF);
        }

        // ---------------------------------------------------------
        // 4. 三角形の生成と提出
        // ---------------------------------------------------------
        if (trail.count < 2)
        {
            return;
        }

        BuildRings(trail, m_rings);
        if (m_rings.size() < 2)
        {
            return;
        }

        m_vertices.clear();
        m_distortVertices.clear();

        EmitSink sink;
        sink.additive = &m_vertices;
        if (trail.distortion > 0.0f)
        {
            sink.distort = &m_distortVertices;
        }

        switch (static_cast<TrailShape>(trail.shape))
        {
        case TrailShape::Cross:
            EmitCross(trail, m_rings, sink);
            break;
        case TrailShape::Tube:
            EmitTube(trail, m_rings, cameraEye, sink);
            break;
        case TrailShape::Ribbon:
        default:
            EmitRibbon(trail, m_rings, sink);
            break;
        }

        // テクスチャはパスがあるときだけ引く
        // ResourceManager はパスでキャッシュするので、毎フレーム呼んでも読み込むのは初回だけ
        ID3D11ShaderResourceView *trailSRV = nullptr;
        if (!trail.texturePath.empty())
        {
            trail.textureHandle = ResourceManager::Instance().LoadTexture(trail.texturePath.c_str());
            if (trail.textureHandle.IsValid())
            {
                trailSRV = ResourceManager::Instance().GetTexture(trail.textureHandle);
            }
        }

        renderer->SubmitTriangleList(m_vertices.data(), m_vertices.size(), trailSRV);
        renderer->SubmitDistortTriangleList(m_distortVertices.data(), m_distortVertices.size());
    });
}



// 実際の描画は同じフレームの TrailPass が行う
REGISTER_RENDER_SYSTEM(TrailSystem, Priority::RenderStage::R08_Main);
