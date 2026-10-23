#include "DemoSwarmSystem.h"
#include "Game/Logics/AI/Drone/Motion/DroneMotion.h" // LookToRotation
#include <chrono>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ============================================================
    // 固定の値（動きの調整値は DemoSwarmTuning）
    // ============================================================

    // 最初の塊で隣と並ぶ間隔 [m]。参考にした ECS デモと同じく、1辺 0.1m のキューブ
    // （DemoSwarmCube.json）を隙間なく並べる。群れは一辺の約60倍まで広がるので、
    // これを大きくするとカメラの描画距離（1000m）に収まらなくなる
    constexpr float kSpacing = 0.1f;

    // 最初に弾ける速さ。中心からの位置にこれを掛けたものが初速になる [1/s]
    constexpr float kBurst = 1.0f;

    // 群れの中心点の高さ（一辺の長さに対する割合と、足す余白 [m]）。
    // 群れは中心点から上下に一辺の約55倍まで広がる（参考にした ECS デモの値で試算）ので、
    // 地面より下へ出ないようにする
    constexpr float kCenterHeightRate   = 60.0f;
    constexpr float kCenterHeightMargin = 10.0f;

    // 黄金角 [rad]。番号ごとにこの角度ずつ回すと、目標点が隊長機の周りにむらなく散らばる
    constexpr float kGoldenAngle = 2.39996323f;

    // 最初の塊の1辺に並ぶ機数
    uint32_t BlockSide(uint32_t count)
    {
        return (std::max)(1u, static_cast<uint32_t>(std::ceil(std::cbrt(static_cast<float>(count)))));
    }

    // 最初の塊の一辺の長さ [m]。調整値はこれに対する割合で持つ
    float BlockLength(uint32_t count)
    {
        return (std::max)(kSpacing, static_cast<float>(BlockSide(count) - 1) * kSpacing);
    }

    Vector3 SwarmCenter(uint32_t count)
    {
        return Vector3(0.0f, BlockLength(count) * kCenterHeightRate + kCenterHeightMargin, 0.0f);
    }

    // 番号から 0〜1 の値を作る。目標点の距離・回る速さを機体ごとにばらすのに使う
    float Hash01(uint32_t seed)
    {
        seed ^= seed >> 15;
        seed *= 2246822519u;
        seed ^= seed >> 13;
        seed *= 3266489917u;
        seed ^= seed >> 16;
        return static_cast<float>(seed & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
    }

    float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    // target へ向かう単位ベクトルに強さを掛けた加速度。重なっているときは 0
    Vector3 PullToward(const Vector3 &from, const Vector3 &target, float strength)
    {
        Vector3 dir = target - from;
        if (dir.LengthSquared() < 1e-6f) return Vector3::Zero;
        dir.Normalize();
        return dir * strength;
    }

    // 速度の向きへ機首を向ける。止まっているときは今の向きのまま
    void FaceVelocity(TransformComponent &trans, Vector3 vel)
    {
        if (vel.LengthSquared() < 1e-6f) return;
        vel.Normalize();
        trans.rotation = CCL::AI::Motion::LookToRotation(vel, true);
    }

    /**
     * @brief 一般機 index の、隊長機から見た目標点の位置を返す。
     *
     * 球面上に散らばった方向（フィボナッチ球）を、機体ごとの速さと向きで水平に回す。
     * 距離も機体ごとにばらすので、目標点は隊長機の周りの厚みのある殻に散らばり、回り続ける。
     */
    Vector3 OrbitOffset(uint32_t index, uint32_t count, float t, float length, const DemoSwarmTuning &tune)
    {
        const float n    = static_cast<float>(count);
        const float y    = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / n;
        const float ring = std::sqrt((std::max)(0.0f, 1.0f - y * y));

        const float radius = length * Lerp(tune.orbitRadiusMin, tune.orbitRadiusMax, Hash01(index * 31u + 7u));
        const float speed  = Lerp(tune.orbitSpeedMin, tune.orbitSpeedMax, Hash01(index * 53u + 11u));
        const float sign   = (Hash01(index * 71u + 3u) < 0.5f) ? 1.0f : -1.0f;

        const float theta = static_cast<float>(index) * kGoldenAngle + sign * speed * t;
        return Vector3(ring * std::cos(theta), y, ring * std::sin(theta)) * radius;
    }
} // namespace

// ---------------------------------------------------------------------------
// 最初の状態
// ---------------------------------------------------------------------------

void DemoSwarmSystem::InitialState(uint32_t index, uint32_t count, Vector3 &outPos, Vector3 &outVel) const
{
    // 処理フロー:
    // 1. 番号から、立方体の塊の中の位置を決める
    // 2. 一般機は中心から外向きに飛び出させる
    // 3. 隊長機は中心点の周りを回る向きへ飛ばす

    // --- 塊の中の位置 ---

    const uint32_t side = BlockSide(count);
    const float    half = static_cast<float>(side - 1) * 0.5f;

    const Vector3 local((static_cast<float>(index % side) - half) * kSpacing,
                        (static_cast<float>((index / side) % side) - half) * kSpacing,
                        (static_cast<float>(index / (side * side)) - half) * kSpacing);

    outPos = SwarmCenter(count) + local;

    // --- 一般機の初速 ---

    // 中心から遠いものほど速く飛び出す。塊が爆発するように弾けて見える
    outVel = local * kBurst;

    if (index != 0) return;

    // --- 隊長機の初速 ---

    // 水平面で中心点の周りを回る向き。円を保つ速さ √(引く強さ×距離) を基準に、
    // leaderOrbit 倍のエネルギーで飛ばして大きなループにする
    Vector3 tangent(-local.z, 0.0f, local.x);
    if (tangent.LengthSquared() < 1e-6f) tangent = Vector3::UnitX;
    tangent.Normalize();

    const float pull  = tuning.leaderPull * BlockLength(count);
    const float speed = std::sqrt((std::max)(0.0f, tuning.leaderOrbit * pull * local.Length()));
    outVel            = tangent * speed;
}

void DemoSwarmSystem::Reset(uint32_t count)
{
    m_droneCount = count;
    m_elapsed    = 0.0f;
    InitialState(0, count, m_leaderPos, m_leaderVel);
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void DemoSwarmSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 隊長機を中心点へ引き寄せて進める（1機だけなのでここで直接）
    // 2. 全機を、隊長機の周りの自分の目標点へ引き寄せて進める（Chunk ごとに Job へ分けて並列）
    // 3. かかった時間を記録する

    const uint32_t count = m_droneCount;
    if (count == 0) return;

    m_elapsed += dt;

    const float   length = BlockLength(count);
    const Vector3 center = SwarmCenter(count);

    // --- 隊長機 ---

    // ブレーキは掛けない。中心点を行き過ぎては引き戻され、毎周少しずつずれるループを描く
    m_leaderVel += PullToward(m_leaderPos, center, tuning.leaderPull * length) * dt;
    m_leaderPos += m_leaderVel * dt;

    // --- 全機 ---

    const auto begin = std::chrono::steady_clock::now();

    // ラムダは別スレッドで走るので、隊長機の状態と調整値は値で持たせる（ループ中に変わらない）
    const Vector3         leaderPos    = m_leaderPos;
    const Vector3         leaderVel    = m_leaderVel;
    const DemoSwarmTuning tune         = tuning;
    const float           t            = m_elapsed;
    const float           followerPull = tune.followerPull * length;
    const float           dragKeep     = (std::max)(0.0f, 1.0f - tune.followerDrag * dt);

    ForEachParallel([=](TransformComponent &trans, DemoSwarmComponent &swarm) {
        // 隊長機は System が進めた状態を写すだけ
        if (swarm.index == 0)
        {
            trans.position = leaderPos;
            FaceVelocity(trans, leaderVel);
            return;
        }

        // 目標点は隊長機そのものか、隊長機の周りを回る自分専用の点
        const Vector3 target = tune.useOrbitTarget
                                   ? leaderPos + OrbitOffset(swarm.index, count, t, length, tune)
                                   : leaderPos;

        // 目標点へ引き寄せ、空気抵抗で少しだけ減速する
        Vector3 vel(swarm.velocity);
        vel += PullToward(Vector3(trans.position), target, followerPull) * dt;
        vel *= dragKeep;

        swarm.velocity = vel;
        trans.position = Vector3(trans.position) + vel * dt;
        FaceVelocity(trans, vel);
    });

    // --- 計測 ---

    // 毎フレームの値は揺れて読めないので、表示用に指数移動平均でならす
    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - begin).count();
    m_updateMs += (ms - m_updateMs) * 0.1f;
}
