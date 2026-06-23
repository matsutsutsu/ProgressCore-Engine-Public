/// @file  CameraDollySystem.cpp
/// @brief CameraDollySystemの実装。弧長パラメータ化を用いた等速移動レールカメラ。

#include "Engine/Camera/Systems/CameraDollySystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

// -------------------------------------------------------------------------
// [既存] 座標取得とCatmull-Rom計算
// -------------------------------------------------------------------------
static const Vector3 &GetPointClamped(const std::vector<Vector3> &pts, int index)
{
    int clamped = std::clamp(index, 0, static_cast<int>(pts.size()) - 1);
    return pts[clamped];
}

static Vector3 EvaluateCatmullRom(const std::vector<Vector3> &pts, float t)
{
    if (pts.empty())
        return Vector3::Zero;
    if (pts.size() == 1)
        return pts[0];

    int   index = static_cast<int>(std::floor(t));
    float frac  = t - std::floor(t);

    XMVECTOR p0 = XMLoadFloat3(&GetPointClamped(pts, index - 1));
    XMVECTOR p1 = XMLoadFloat3(&GetPointClamped(pts, index));
    XMVECTOR p2 = XMLoadFloat3(&GetPointClamped(pts, index + 1));
    XMVECTOR p3 = XMLoadFloat3(&GetPointClamped(pts, index + 2));

    XMVECTOR result = XMVectorCatmullRom(p0, p1, p2, p3, frac);

    Vector3 finalPos;
    XMStoreFloat3(&finalPos, result);
    return finalPos;
}

// -------------------------------------------------------------------------
// [新規] LUT (Look-Up Table) の構築
// JSONロード直後に1回だけ実行し、軌道の「距離とtの関係」をキャッシュする
// -------------------------------------------------------------------------
static void BuildLookupTable(CameraPathAsset &asset)
{
    asset.lut.clear();
    if (asset.points.size() < 2)
        return;

    int   resolutionPerSegment = 20; // 1区間あたりの分割数（精度）
    int   totalSamples         = static_cast<int>(asset.points.size() - 1) * resolutionPerSegment;
    float maxT                 = static_cast<float>(asset.points.size() - 1);

    asset.lut.push_back({0.0f, 0.0f}); // 始点
    Vector3 prevPos             = EvaluateCatmullRom(asset.points, 0.0f);
    float   accumulatedDistance = 0.0f;

    for (int i = 1; i <= totalSamples; ++i)
    {
        float   t       = (static_cast<float>(i) / totalSamples) * maxT;
        Vector3 currPos = EvaluateCatmullRom(asset.points, t);

        // 距離を積算
        accumulatedDistance += Vector3::Distance(prevPos, currPos);
        asset.lut.push_back({accumulatedDistance, t});

        prevPos = currPos;
    }
    asset.totalLength = accumulatedDistance;
}

// -------------------------------------------------------------------------
// [新規] 距離(m)からパラメータ t を逆算する (LUTの二分探索)
// -------------------------------------------------------------------------
static float GetTFromDistance(const CameraPathAsset &asset, float targetDist)
{
    if (asset.lut.empty())
        return 0.0f;
    if (targetDist <= 0.0f)
        return 0.0f;
    if (targetDist >= asset.totalLength)
        return asset.lut.back().t;

    // 二分探索 (std::lower_bound) で目標距離に近いサンプルを見つける
    auto it = std::lower_bound(asset.lut.begin(), asset.lut.end(), targetDist,
        [](const PathSample &sample, float dist) {
            return sample.distance < dist;
        });

    if (it == asset.lut.begin())
        return it->t;

    // 前後のサンプルを取得し、線形補間(Lerp)でより正確な t を求める
    auto  prevIt      = it - 1;
    float distSegment = it->distance - prevIt->distance;

    if (distSegment <= 0.0001f)
        return it->t; // ゼロ除算防止

    float lerpFactor = (targetDist - prevIt->distance) / distSegment;
    return std::lerp(prevIt->t, it->t, lerpFactor);
}

// -------------------------------------------------------------------------
// システムの Update 実装
// -------------------------------------------------------------------------
void CameraDollySystem::Update(float /*rawDt*/)
{
    // 警告: 物理時間は必ず time.localDt を使う

    ForEach([](VirtualCamera &vcam, CameraDollyCartComponent &cart, const TimeStateComponent &time) {
        // 1. リソースの遅延ロードと LUT の構築
        if (!cart.loadedPath && !cart.pathAsset.empty())
        {
            std::string jsonStr = FileSystem::ReadAllText(cart.pathAsset);
            if (!jsonStr.empty())
            {
                try
                {
                    auto j          = nlohmann::json::parse(jsonStr);
                    cart.loadedPath = std::make_shared<CameraPathAsset>();

                    if (j.contains("points") && j["points"].is_array())
                    {
                        for (auto &p : j["points"])
                        {
                            cart.loadedPath->points.emplace_back(
                                p.value("x", 0.0f), p.value("y", 0.0f), p.value("z", 0.0f));
                        }
                    }

                    // ★追加：ロード直後にLUTを構築する
                    BuildLookupTable(*cart.loadedPath);

                    CCL_LOG_SUCCESS(LogCategory::Game, "Loaded Camera Path: %s (Length: %.2fm)",
                        cart.pathAsset.c_str(), cart.loadedPath->totalLength);
                }
                catch (const std::exception &e)
                {
                    CCL_LOG_ERROR(LogCategory::Game, "Failed to parse Camera Path: %s\n%s", cart.pathAsset.c_str(), e.what());
                }
            }
        }

        if (!cart.loadedPath || cart.loadedPath->points.size() < 2)
            return;

        const auto &asset = *cart.loadedPath;

        // 2. 進行度の更新（メートル単位での等速移動）
        cart.currentDistance += cart.moveSpeed * time.localDt;

        // 終端処理
        cart.currentDistance = std::clamp(cart.currentDistance, 0.0f, asset.totalLength);

        // 3. 距離から t を逆算し、座標を補間
        float   currentT = GetTFromDistance(asset, cart.currentDistance);
        Vector3 pos      = EvaluateCatmullRom(asset.points, currentT);
        vcam.resultPos   = pos;

        // 4. 注視点 (LookAt) の計算
        if (cart.lookForward)
        {
            // ★圧倒的改善：lookAheadOffset が「メートル単位」になったため、
            // 「常に 2m 先の軌道を見る」といった直感的な制御が成立する。
            float aheadDist = std::clamp(cart.currentDistance + cart.lookAheadOffset, 0.0f, asset.totalLength);

            if (aheadDist == cart.currentDistance && cart.currentDistance > 0.0f)
            {
                // 終点付近のフォールバック
                float   prevT   = GetTFromDistance(asset, cart.currentDistance - 0.1f);
                Vector3 prevPos = EvaluateCatmullRom(asset.points, prevT);
                Vector3 dir     = pos - prevPos;
                dir.Normalize();
                vcam.resultLookAt = pos + dir;
            }
            else
            {
                float aheadT      = GetTFromDistance(asset, aheadDist);
                vcam.resultLookAt = EvaluateCatmullRom(asset.points, aheadT);
            }
        }

        vcam.resultUp = Vector3::Up;
        vcam.isValid  = true;
    });
}
REGISTER_LOGIC_SYSTEM(CameraDollySystem, Priority::LogicStage::L02_Update);