#include "TrailSystem.h"

#include "Engine/Graphics/Renderer/TrailRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
    constexpr int SUBDIVISIONS = 4; // 1区間を補間で何分割するか
}



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
        // 途中を間引くと補間の制御点が入れ替わり、尾の形が跳ねる
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
    if (trail.count < 2) return;

    const int   MAX     = TrailComponent::MAX_SEGMENTS;
    const float invLife = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;

    // 古い方から index 番目の断面。範囲の外は端の断面を返す
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

        // 区間の終わり（t=1）は次の区間の始まりと同じなので、最後の区間でだけ含める
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
    // 残りの寿命の割合（1 で刻んだ直後、0 で消える直前）
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

    // Catmull-Rom スプラインの式
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
    // 末尾の断面には次がないので、1つ前の向きを使う
    const size_t a = (i + 1 < rings.size()) ? i : (i - 1);
    const size_t b = a + 1;

    XMVECTOR dir = XMVectorSubtract(XMLoadFloat3(&rings[b].center), XMLoadFloat3(&rings[a].center));

    // 前後の断面が重なって 0 ベクトルになったら、上向きで代用する
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-12f) dir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    return XMVector3Normalize(dir);
}



void TrailSystem::RingBasis(DirectX::FXMVECTOR       tangent,
                            const DirectX::XMFLOAT3 &half,
                            DirectX::XMVECTOR       &outU,
                            DirectX::XMVECTOR       &outV) const
{
    // half から進行方向の成分を引いて、断面の中の1本目の軸にする
    // 板の向きを基準にするので、thickness が 0 なら筒が板と同じ太さになる
    XMVECTOR u = XMLoadFloat3(&half);
    u          = XMVectorSubtract(u, XMVectorScale(tangent, XMVectorGetX(XMVector3Dot(u, tangent))));

    if (XMVectorGetX(XMVector3LengthSq(u)) < 1e-12f)
    {
        // 断面がつぶれているので、進行方向と真上（真上に近いなら X 軸）の外積で代用する
        // 根本と先端を同じ点に置いたトレイル（足元など）は、いつもこちらを通る
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
    if (trail.thickness > 0.0f) return trail.thickness * ring.taper;

    // thickness が 0 なら、板と同じ太さ（根本から先端までの長さの半分）にする
    return XMVectorGetX(XMVector3Length(XMLoadFloat3(&ring.half)));
}



// ---------------------------------------------------------------------------
// 形ごとの三角形の生成
// ---------------------------------------------------------------------------

void TrailSystem::EmitRibbon(const std::vector<Ring> &rings, std::vector<TrailPoint> &outPoints) const
{
    for (size_t i = 0; i + 1 < rings.size(); ++i)
    {
        const Ring &r0 = rings[i];
        const Ring &r1 = rings[i + 1];

        const XMFLOAT3 b0 = {r0.center.x - r0.half.x, r0.center.y - r0.half.y, r0.center.z - r0.half.z};
        const XMFLOAT3 t0 = {r0.center.x + r0.half.x, r0.center.y + r0.half.y, r0.center.z + r0.half.z};
        const XMFLOAT3 b1 = {r1.center.x - r1.half.x, r1.center.y - r1.half.y, r1.center.z - r1.half.z};
        const XMFLOAT3 t1 = {r1.center.x + r1.half.x, r1.center.y + r1.half.y, r1.center.z + r1.half.z};

        // V は幅方向（根本 0、先端 1）
        const TrailPoint pb0 = {b0, r0.age, 0.0f};
        const TrailPoint pt0 = {t0, r0.age, 1.0f};
        const TrailPoint pb1 = {b1, r1.age, 0.0f};
        const TrailPoint pt1 = {t1, r1.age, 1.0f};

        // 四角形を三角形2枚にして積む
        outPoints.insert(outPoints.end(), {pb0, pt0, pt1, pb0, pt1, pb1});
    }
}



void TrailSystem::EmitTube(const TrailComponent    &trail,
                           const std::vector<Ring> &rings,
                           std::vector<TrailPoint> &outPoints)
{
    const int sides = std::clamp(trail.tubeSides, 3, 16);

    // 断面ごとの軸と半径を先に求めておく
    // 1つの断面は前後2つの区間で使うので、頂点ごとに作り直さずに済む
    m_tubeFrames.clear();

    for (size_t i = 0; i < rings.size(); ++i)
    {
        TubeFrame f;
        RingBasis(RingTangent(rings, i), rings[i].half, f.u, f.v);
        f.radius = RingRadius(trail, rings[i]);
        m_tubeFrames.push_back(f);
    }

    // 断面 i の円周の k 番目の点
    auto ringVertex = [&](size_t i, int k) -> XMFLOAT3 {
        const TubeFrame &frame = m_tubeFrames[i];

        const float    angle = XM_2PI * static_cast<float>(k) / static_cast<float>(sides);
        const XMVECTOR dir =
            XMVectorAdd(XMVectorScale(frame.u, std::cos(angle)),
                        XMVectorScale(frame.v, std::sin(angle)));

        XMFLOAT3 pos;
        XMStoreFloat3(&pos, XMVectorAdd(XMLoadFloat3(&rings[i].center), XMVectorScale(dir, frame.radius)));
        return pos;
    };

    // 隣り合う2つの断面の、隣り合う2点ずつで四角形を作る
    for (size_t i = 0; i + 1 < rings.size(); ++i)
    {
        for (int k = 0; k < sides; ++k)
        {
            // 最後の点の次は最初の点に戻して、円を閉じる
            const int k1 = (k + 1) % sides;

            const XMFLOAT3 p00 = ringVertex(i, k);
            const XMFLOAT3 p01 = ringVertex(i, k1);
            const XMFLOAT3 p10 = ringVertex(i + 1, k);
            const XMFLOAT3 p11 = ringVertex(i + 1, k1);

            // V は円周の位置。余りを取る前の k + 1 を使う
            // 余りを取ると最後の面だけ V が逆向きになり、テクスチャが1面に詰まる
            const float vK  = static_cast<float>(k) / static_cast<float>(sides);
            const float vK1 = static_cast<float>(k + 1) / static_cast<float>(sides);

            const TrailPoint v00 = {p00, rings[i].age, vK};
            const TrailPoint v01 = {p01, rings[i].age, vK1};
            const TrailPoint v10 = {p10, rings[i + 1].age, vK};
            const TrailPoint v11 = {p11, rings[i + 1].age, vK1};

            // 四角形を三角形2枚にして積む
            outPoints.insert(outPoints.end(), {v00, v01, v11, v00, v11, v10});
        }
    }
}



// ---------------------------------------------------------------------------
// 色を塗る
// ---------------------------------------------------------------------------

void TrailSystem::PaintPoints(const TrailComponent &trail, const std::vector<TrailPoint> &points)
{
    const float invLife     = (trail.lifeTime > 0.0f) ? (1.0f / trail.lifeTime) : 0.0f;
    const bool  withDistort = trail.distortion > 0.0f;

    for (const TrailPoint &point : points)
    {
        // 尾ほど薄くする
        const float fade = (std::max)(1.0f - point.age * invLife, 0.0f);

        // U は尾の長さ方向（先端 0、末尾 1）
        const float uAlong = std::clamp(point.age * invLife, 0.0f, 1.0f);

        TrailRenderer::Vertex additive;
        additive.position = point.pos;
        additive.color    = XMFLOAT4(trail.color.x, trail.color.y, trail.color.z, trail.color.w * fade);
        additive.uv       = {
            uAlong * trail.uvTiling.x + trail.uvScrollOffset.x,
            point.v * trail.uvTiling.y + trail.uvScrollOffset.y};
        m_vertices.push_back(additive);

        if (!withDistort) continue;

        // 歪みの頂点は、同じ位置と UV のまま、色の欄に歪みのパラメータを入れる
        // 加算の色のアルファは掛けない。見た目は薄くても強く歪ませられるようにするため
        TrailRenderer::Vertex distort = additive;
        distort.color                 = XMFLOAT4(trail.distortion, 0.0f, 0.0f, fade);
        m_distortVertices.push_back(distort);
    }
}



// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------

void TrailSystem::Update(float dt)
{
    if (!m_world->HasResource<TrailRenderer *>()) return;

    TrailRenderer *renderer = m_world->GetResource<TrailRenderer *>();
    if (!renderer) return;

    // 描画の段は一時停止中も走るので、止まっている間は模様の流れも止める
    const float presentDt = Time::PresentationDt(*m_world, dt);

    ForEach([&](const TransformComponent &transform, TrailComponent &trail) {
        // 処理フロー:
        // 1. UV スクロールを進める
        // 2. 今の刃の根本と先端をワールドへ移す
        // 3. 新しい断面を刻むか決める
        // 4. 断面の列から三角形の形を作り、色を塗って積む

        // ---------------------------------------------------------
        // 1. UV スクロール
        // ---------------------------------------------------------

        // 模様の流れは描画の dt で進める
        // 尾の形には関わらないので、リフレッシュレートで速さは変わらない

        // スロー中は、尾の老化と同じ倍率で遅くする
        // 模様だけ等速で流れると、尾が時間の流れから浮いて見える
        const float scrollDt = presentDt * trail.timeScale;

        // 桁あふれしないよう、一定の値で巻き戻す
        trail.uvScrollOffset.x = std::fmod(trail.uvScrollOffset.x + trail.uvScroll.x * scrollDt, 1024.0f);
        trail.uvScrollOffset.y = std::fmod(trail.uvScrollOffset.y + trail.uvScroll.y * scrollDt, 1024.0f);

        // ---------------------------------------------------------
        // 2. 刃の根本と先端
        // ---------------------------------------------------------

        // 位置なので、平行移動も含めて変換する
        const XMMATRIX W     = XMLoadFloat4x4(&transform.renderWorldMatrix);
        const XMVECTOR baseW = XMVector3Transform(XMLoadFloat3(&trail.baseOffset), W);
        const XMVECTOR tipW  = XMVector3Transform(XMLoadFloat3(&trail.tipOffset), W);

        XMFLOAT3 baseF, tipF;
        XMStoreFloat3(&baseF, baseW);
        XMStoreFloat3(&tipF, tipW);

        // ---------------------------------------------------------
        // 3. 断面を刻むか
        // ---------------------------------------------------------

        // 刻む頻度はロジックの印で、刻む位置は画面に見えている刃（renderWorldMatrix）で決める
        // 両方を描画側にすると尾が二重の線になり、両方をロジックにすると尾が刃から離れる
        const bool requested  = trail.sampleRequested;
        trail.sampleRequested = false;

        // 前の断面から先端が minDistance 以上動いたときだけ刻む
        bool record = requested;
        if (record && trail.count > 0)
        {
            const int      newest  = (trail.head + trail.count - 1) % TrailComponent::MAX_SEGMENTS;
            const XMVECTOR lastTip = XMLoadFloat3(&trail.segments[newest].tip);
            const float    movedSq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(tipW, lastTip)));

            if (movedSq < trail.minDistance * trail.minDistance) record = false;
        }

        if (record) RecordSegment(trail, baseF, tipF);

        // ---------------------------------------------------------
        // 4. 三角形の生成と提出
        // ---------------------------------------------------------
        if (trail.count < 2) return;

        // 記録した断面を補間して、断面の列にする
        BuildRings(trail, m_rings);
        if (m_rings.size() < 2) return;

        m_points.clear();
        m_vertices.clear();
        m_distortVertices.clear();

        // 断面の列から、形に応じた三角形の点を並べる
        switch (static_cast<TrailShape>(trail.shape))
        {
        case TrailShape::Tube:
            EmitTube(trail, m_rings, m_points);
            break;
        case TrailShape::Ribbon:
        default:
            EmitRibbon(m_rings, m_points);
            break;
        }

        // 並べた点に色を塗り、加算と歪みの層の頂点にする
        PaintPoints(trail, m_points);

        // テクスチャはパスがあるときだけ引く。空なら TrailRenderer が白を使う
        // ResourceManager はパスでキャッシュするので、毎フレーム呼んでも読み込むのは初回だけ
        ID3D11ShaderResourceView *trailSRV = nullptr;
        if (!trail.texturePath.empty())
        {
            trail.textureHandle = ResourceManager::Instance().LoadTexture(trail.texturePath.c_str());
            if (trail.textureHandle.IsValid()) trailSRV = ResourceManager::Instance().GetTexture(trail.textureHandle);
        }

        // 加算と歪みの三角形を TrailRenderer にためる（描くのは TrailPass）
        renderer->SubmitTriangleList(m_vertices.data(), m_vertices.size(), trailSRV);
        renderer->SubmitDistortTriangleList(m_distortVertices.data(), m_distortVertices.size());
    });
}



REGISTER_RENDER_SYSTEM(TrailSystem, Priority::RenderStage::R08_Main);
