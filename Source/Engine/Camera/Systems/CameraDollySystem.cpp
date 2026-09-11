// CameraDollySystemの実装。弧長パラメータ化を用いた等速移動レールカメラ。

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
// 座標取得とCatmull-Rom計算
// -------------------------------------------------------------------------
const Vector3 &CameraDollySystem::GetPoint(const std::vector<Vector3> &pts, int index, bool loop)
{
    const int n = static_cast<int>(pts.size());
    if (loop)
    {
        // 閉曲線: 端をまたいだら反対側へ巻き戻す。
        // これをやらないと、始点/終点で接線が折れて継ぎ目が見える。
        int i = index % n;
        if (i < 0) i += n;
        return pts[i];
    }
    return pts[std::clamp(index, 0, n - 1)];
}

Vector3 CameraDollySystem::EvaluateCatmullRom(const std::vector<Vector3> &pts, float t, bool loop)
{
    if (pts.empty())
        return Vector3::Zero;
    if (pts.size() == 1)
        return pts[0];

    int   index = static_cast<int>(std::floor(t));
    float frac  = t - std::floor(t);

    XMVECTOR p0 = XMLoadFloat3(&GetPoint(pts, index - 1, loop));
    XMVECTOR p1 = XMLoadFloat3(&GetPoint(pts, index, loop));
    XMVECTOR p2 = XMLoadFloat3(&GetPoint(pts, index + 1, loop));
    XMVECTOR p3 = XMLoadFloat3(&GetPoint(pts, index + 2, loop));

    XMVECTOR result = XMVectorCatmullRom(p0, p1, p2, p3, frac);

    Vector3 finalPos;
    XMStoreFloat3(&finalPos, result);
    return finalPos;
}

float CameraDollySystem::MaxParam(const CameraPathAsset &asset)
{
    // 開曲線は points-1 区間。閉曲線は「最後の点 → 始点」の区間が増えるので points 区間。
    const auto n = static_cast<float>(asset.points.size());
    return asset.isLoop ? n : (n - 1.0f);
}

// -------------------------------------------------------------------------
// LUT (Look-Up Table) の構築
// JSONロード直後に1回だけ実行し、軌道の「距離とtの関係」をキャッシュする
// -------------------------------------------------------------------------
void CameraDollySystem::BuildLookupTable(CameraPathAsset &asset)
{
    asset.lut.clear();
    asset.totalLength = 0.0f;
    if (asset.points.size() < 2)
        return;

    const int   resolutionPerSegment = 20; // 1区間あたりの分割数（精度）
    const float maxT                 = MaxParam(asset);
    const int   totalSamples         = static_cast<int>(maxT) * resolutionPerSegment;
    if (totalSamples <= 0)
        return;

    asset.lut.push_back({0.0f, 0.0f}); // 始点
    Vector3 prevPos             = EvaluateCatmullRom(asset.points, 0.0f, asset.isLoop);
    float   accumulatedDistance = 0.0f;

    for (int i = 1; i <= totalSamples; ++i)
    {
        float   t       = (static_cast<float>(i) / totalSamples) * maxT;
        Vector3 currPos = EvaluateCatmullRom(asset.points, t, asset.isLoop);

        // 距離を積算
        accumulatedDistance += Vector3::Distance(prevPos, currPos);
        asset.lut.push_back({accumulatedDistance, t});

        prevPos = currPos;
    }
    asset.totalLength = accumulatedDistance;
}

// -------------------------------------------------------------------------
// 距離(m)からパラメータ t を逆算する (LUTの二分探索)
// -------------------------------------------------------------------------
float CameraDollySystem::GetTFromDistance(const CameraPathAsset &asset, float targetDist)
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

float CameraDollySystem::WrapDistance(float dist, float totalLength)
{
    if (totalLength <= 0.0f)
        return 0.0f;
    float d = std::fmod(dist, totalLength);
    if (d < 0.0f) d += totalLength;
    return d;
}

// -------------------------------------------------------------------------
// パス（JSON）の読み込み
// -------------------------------------------------------------------------
void CameraDollySystem::LoadPathIfNeeded(CameraDollyCartComponent &cart) const
{
    // 未ロード かつ エラー未発生 の場合のみ試行
    if (cart.loadedPath || cart.pathAsset.empty() || cart.hasLoadError)
        return;

    std::string jsonStr = FileSystem::ReadAllText(cart.pathAsset);
    if (jsonStr.empty())
    {
        cart.hasLoadError = true; // ファイル不在または空なら再試行しない
        CCL_LOG_ERROR(LogCategory::Game, "Camera Path file not found or empty: %s", cart.pathAsset.c_str());
        return;
    }

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

        // isLoop は構造体に存在するのに一度も読まれていなかった（＝死にフィールド）。
        // エディタ側の書き出しキー揺れに備えて "loop" も受ける。
        cart.loadedPath->isLoop = j.value("isLoop", j.value("loop", false));

        BuildLookupTable(*cart.loadedPath);

        CCL_LOG_SUCCESS(LogCategory::Game, "Loaded Camera Path: %s (Length: %.2fm, Loop: %s)",
            cart.pathAsset.c_str(), cart.loadedPath->totalLength,
            cart.loadedPath->isLoop ? "yes" : "no");
    }
    catch (const std::exception &e)
    {
        cart.hasLoadError = true; // パース失敗なら再試行しない
        cart.loadedPath   = nullptr;
        CCL_LOG_ERROR(LogCategory::Game, "Failed to parse Camera Path: %s\n%s", cart.pathAsset.c_str(), e.what());
    }
}

// -------------------------------------------------------------------------
// システムの Update 実装
// -------------------------------------------------------------------------
void CameraDollySystem::Update(float /*rawDt*/)
{
    // 警告: 物理時間は必ず time.localDt を使う

    ForEach([this](VirtualCamera &vcam, CameraDollyCartComponent &cart, const TimeStateComponent &time) {
        // 1. リソースの遅延ロード
        //    ※ 同期I/O + JSONパースを Update 内で行うため、初回フレームでヒッチする。
        //       将来はアセットマネージャ側へ移し、パス単位でキャッシュを共有すること。
        LoadPathIfNeeded(cart);

        if (!cart.loadedPath || cart.loadedPath->points.size() < 2 || cart.loadedPath->totalLength <= 0.0f)
            return;

        const auto &asset = *cart.loadedPath;

        // 2. 進行度の更新（メートル単位での等速移動）
        cart.currentDistance += cart.moveSpeed * time.localDt;

        // 終端処理: 閉曲線なら巻き戻し、開曲線なら端で止める
        cart.currentDistance = asset.isLoop
                                   ? WrapDistance(cart.currentDistance, asset.totalLength)
                                   : std::clamp(cart.currentDistance, 0.0f, asset.totalLength);

        // 3. 距離から t を逆算し、座標を補間
        float   currentT = GetTFromDistance(asset, cart.currentDistance);
        Vector3 pos      = EvaluateCatmullRom(asset.points, currentT, asset.isLoop);
        vcam.resultPos   = pos;

        // 4. 注視点 (LookAt) の計算
        if (cart.lookForward)
        {
            // lookAheadOffset が「メートル単位」なので、
            // 「常に 2m 先の軌道を見る」といった直感的な制御が成立する。
            if (asset.isLoop)
            {
                // ループなら先読み位置は必ず軌道上に存在する。端の特別扱いは不要。
                const float aheadDist = WrapDistance(cart.currentDistance + cart.lookAheadOffset, asset.totalLength);
                const float aheadT    = GetTFromDistance(asset, aheadDist);
                vcam.resultLookAt     = EvaluateCatmullRom(asset.points, aheadT, asset.isLoop);
            }
            else
            {
                const float aheadDist = std::clamp(cart.currentDistance + cart.lookAheadOffset, 0.0f, asset.totalLength);

                // 終点に到達して先読みできない場合のフォールバック。
                // float の == 比較は両者が同じ clamp を通る偶然に依存していて脆いので、
                // 「先読みぶんの距離が実質ゼロか」を許容誤差で判定する。
                const bool noLookahead = (aheadDist - cart.currentDistance) < 1e-4f;

                if (noLookahead && cart.currentDistance > 0.0f)
                {
                    // 終点付近: 少し手前との差分から進行方向を求めて、その先を見る
                    float   prevT   = GetTFromDistance(asset, (std::max)(0.0f, cart.currentDistance - 0.1f));
                    Vector3 prevPos = EvaluateCatmullRom(asset.points, prevT, asset.isLoop);
                    Vector3 dir     = pos - prevPos;
                    if (dir.LengthSquared() > 1e-8f)
                    {
                        dir.Normalize();
                        vcam.resultLookAt = pos + dir;
                    }
                }
                else
                {
                    float aheadT      = GetTFromDistance(asset, aheadDist);
                    vcam.resultLookAt = EvaluateCatmullRom(asset.points, aheadT, asset.isLoop);
                }
            }
        }

        vcam.resultUp       = Vector3::Up;
        vcam.posInitialized = true;
        // lookForward=false のときは注視点を誰も書いていない。ここで確定扱いにすると
        // 未初期化の resultLookAt のまま Brain に採用されてしまうため、
        // 併載された CameraAimLookAt が書くまで有効化しない。
        if (cart.lookForward) vcam.lookInitialized = true;
        vcam.RefreshValid();
    });
}
REGISTER_LOGIC_SYSTEM(CameraDollySystem, Priority::LogicStage::L02_Update);
