#include "ShapeSampler.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <random>

#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Platform/Core/Logger.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    // 表面に落とす候補の数（出力する点の何倍か）。少ないと最遠点で選ぶ余地が無く、偶然の密集・隙間が残る
    constexpr int kCandidatesPerPoint = 8;

    constexpr uint32_t kSeed = 20261003u; // 乱数の種。固定して、同じモデルからは毎回同じ点を出す

    struct Triangle
    {
        Vector3 a, b, c; // 3頂点（モデル空間）
    };

    /**
     * @brief 各ノードの初期姿勢でのモデル空間の行列を求める（描画と同じ位置に点を置くため、AABB の計算と同じ式）。
     *
     * glTF のノードは親が先に並ぶとは限らないので、親を再帰で先に求める。
     *
     * @param model 行列を求めるモデル。
     * @return ノードと同じ添字で並んだ行列の列。
     */
    std::vector<XMFLOAT4X4> ComputeNodeGlobals(const ModelResource &model)
    {
        const auto &nodes = model.GetNodes();

        std::vector<XMFLOAT4X4> globals(nodes.size());
        std::vector<bool>       computed(nodes.size(), false);

        std::function<XMMATRIX(int)> globalOf = [&](int index) -> XMMATRIX {
            if (computed[index]) return XMLoadFloat4x4(&globals[index]);

            const auto    &node  = nodes[index];
            const XMMATRIX local = XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) *
                                   XMMatrixRotationQuaternion(XMLoadFloat4(&node.rotate)) *
                                   XMMatrixTranslation(node.translate.x, node.translate.y, node.translate.z);
            const XMMATRIX global = (node.parentIndex >= 0) ? local * globalOf(node.parentIndex) : local;

            XMStoreFloat4x4(&globals[index], global);
            computed[index] = true;
            return global;
        };

        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
            globalOf(i);
        return globals;
    }

    // モデルの全三角形をモデル空間で集める。
    std::vector<Triangle> CollectTriangles(const ModelResource &model)
    {
        const std::vector<XMFLOAT4X4> globals = ComputeNodeGlobals(model);

        std::vector<Triangle> tris;
        for (const auto &mesh : model.GetMeshes())
        {
            // 描くときと同じく、スキンメッシュ以外はノードの行列を掛けた位置に置く。
            // スキンメッシュの頂点は最初からモデル空間（バインド姿勢）にある。
            XMMATRIX toModel = XMMatrixIdentity();
            if (mesh.bones.empty() && mesh.nodeIndex >= 0 && mesh.nodeIndex < static_cast<int>(globals.size()))
                toModel = XMLoadFloat4x4(&globals[mesh.nodeIndex]);

            const auto &v = mesh.vertices;
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            {
                const UINT i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
                if (i0 >= v.size() || i1 >= v.size() || i2 >= v.size()) continue;

                Triangle t;
                t.a = Vector3::Transform(Vector3(v[i0].position), toModel);
                t.b = Vector3::Transform(Vector3(v[i1].position), toModel);
                t.c = Vector3::Transform(Vector3(v[i2].position), toModel);
                tris.push_back(t);
            }
        }
        return tris;
    }

    /**
     * @brief 面積に比例して、表面に count 個の点を落とす。
     *
     * 頂点をそのまま使うと、細かく分割された部分（柄の装飾など）に点が偏る。
     *
     * @param tris  点を落とす三角形の列（モデル空間）。
     * @param count 落とす点の数。
     * @return 落とした点の列。三角形の面積の合計が 0 のときは空。
     */
    std::vector<Vector3> SampleCandidates(const std::vector<Triangle> &tris, int count)
    {
        // 面積の累積。乱数をこの範囲で振り、二分探索で三角形を選ぶ。
        std::vector<float> cumulative(tris.size());
        float              total = 0.0f;
        for (size_t i = 0; i < tris.size(); ++i)
        {
            const Triangle &t = tris[i];
            total += 0.5f * (t.b - t.a).Cross(t.c - t.a).Length();
            cumulative[i] = total;
        }

        std::vector<Vector3> points;
        if (total <= 0.0f) return points;

        std::mt19937                          rng(kSeed);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);

        points.reserve(count);
        for (int n = 0; n < count; ++n)
        {
            const float  pick = uni(rng) * total;
            const size_t idx  = (std::min)(
                static_cast<size_t>(std::lower_bound(cumulative.begin(), cumulative.end(), pick) - cumulative.begin()),
                tris.size() - 1);
            const Triangle &t = tris[idx];

            // 三角形の中の一様な点。r1 の平方根を取らないと、頂点 a の近くに偏る。
            const float s  = std::sqrt(uni(rng));
            const float r2 = uni(rng);
            points.push_back(t.a * (1.0f - s) + t.b * (s * (1.0f - r2)) + t.c * (s * r2));
        }
        return points;
    }

    /**
     * @brief 最遠点サンプリング。すでに選んだ点から一番遠い候補を順に選んで並べる。
     *
     * 最初の1点は重心から一番遠い点にして、毎回同じ並びにする。
     *
     * @param candidates 選ぶ元の候補点。
     * @param count      選ぶ点の数。候補より多いときは候補の数まで。
     * @return 選んだ順に並べた点の列。
     */
    std::vector<Vector3> FarthestPointOrder(const std::vector<Vector3> &candidates, int count)
    {
        std::vector<Vector3> ordered;
        if (candidates.empty()) return ordered;

        count = (std::min)(count, static_cast<int>(candidates.size()));
        ordered.reserve(count);

        Vector3 centroid = Vector3::Zero;
        for (const Vector3 &p : candidates)
            centroid += p;
        centroid /= static_cast<float>(candidates.size());

        size_t first = 0;
        float  best  = -1.0f;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const float d = Vector3::DistanceSquared(candidates[i], centroid);
            if (d > best)
            {
                best  = d;
                first = i;
            }
        }

        // 各候補から「選んだ点のうち一番近いもの」までの距離。選ぶたびに縮めていく。
        std::vector<float> nearest(candidates.size(), FLT_MAX);

        size_t current = first;
        for (int k = 0; k < count; ++k)
        {
            ordered.push_back(candidates[current]);

            size_t next     = current;
            float  farthest = -1.0f;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                nearest[i] = (std::min)(nearest[i], Vector3::DistanceSquared(candidates[i], candidates[current]));
                if (nearest[i] > farthest)
                {
                    farthest = nearest[i];
                    next     = i;
                }
            }
            current = next;
        }
        return ordered;
    }
} // namespace

namespace CCL::AI::Formation
{

    std::vector<Vector3> SampleModelSurface(const std::string &modelPath, int maxPoints)
    {
        // 処理フロー:
        // 1. モデルを読む（描画と同じキャッシュを使う）
        // 2. 三角形を集める
        // 3. 面積比例で候補を落とす
        // 4. 最遠点の順に並べる

        if (modelPath.empty() || maxPoints <= 0) return {};

        // 1,モデルを読む
        auto              &resources = ResourceManager::Instance();
        const ModelHandle  handle    = resources.LoadModelResource(modelPath.c_str());
        const ModelResource *model   = resources.GetModel(handle);
        if (!model)
        {
            CCL_LOG_ERROR(LogCategory::AI, "[Formation] モデルを読めない: %s", modelPath.c_str());
            return {};
        }

        // 2,モデルの三角形を集める
        const std::vector<Triangle> tris = CollectTriangles(*model);
        if (tris.empty())
        {
            CCL_LOG_ERROR(LogCategory::AI, "[Formation] モデルに三角形が無い: %s", modelPath.c_str());
            return {};
        }

        // 3,表面に目標座標である候補の点をばらまく
        const std::vector<Vector3> candidates = SampleCandidates(tris, maxPoints * kCandidatesPerPoint);
        return FarthestPointOrder(candidates, maxPoints);
    }

} // namespace CCL::AI::Formation
