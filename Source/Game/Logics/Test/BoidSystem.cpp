#include "BoidSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <imgui.h>
#include "Engine/Timer/TimeState.h"
#include <cmath>

void BoidSystem::Update(float dt)
{
    // ====================================================================
    // TimeContext の適用 (VoxelOceanSystem と同じく相対時間に対応)
    // ====================================================================
    if (!_world || !_world->HasResource<TimeContext>()) return;
    auto &ctx = _world->GetResource<TimeContext>();

    // 適切なスケールを採用（ゲームロジックなら globalScale が基本）
    float timeScale = ctx.globalScale;
    float scaledDt  = dt * timeScale;

    // ポーズ中・完全ヒットストップ中は計算ごとスキップして負荷をゼロにする
    if (scaledDt <= 0.0f) return;

    using namespace DirectX::SimpleMath;

    // ====================================================================
    // フェーズ1: Snapshot（スナップショット）の作成 (シングルスレッド)
    // ====================================================================
    m_snapshot.clear();
    m_snapshot.reserve(10000);

    // BoidComponent はこのフェーズでは Read-Only
    ForEach([&](TransformComponent &trans, const BoidComponent &boid) {
        m_snapshot.push_back({trans.position, boid.velocity});
    });

    size_t totalBoids = m_snapshot.size();
    if (totalBoids == 0) return;

    // ポインタと値コピーを準備（ラムダキャプチャを通じてレジスタに乗せる）
    const BoidData *snapshotPtr = m_snapshot.data();
    BoidSettings    settings    = m_settings;

    // ====================================================================
    // フェーズ2: Boidsの行動計算と位置更新 (マルチスレッド / Parallel For)
    // ====================================================================
    // settings を値渡し(コピー)でキャプチャすることで、this-> 経由のキャッシュミスを排除
    ForEachParallel([snapshotPtr, totalBoids, scaledDt, settings](TransformComponent &trans, BoidComponent &boid) {
        Vector3 pos = trans.position;
        Vector3 vel = boid.velocity;

        Vector3 separation = Vector3::Zero;
        Vector3 alignment  = Vector3::Zero;
        Vector3 cohesion   = Vector3::Zero;

        int   neighborCount = 0;
        float radiusSq      = settings.neighborRadius * settings.neighborRadius;

        // O(N) ループ: snapshotPtr は連続メモリ空間なのでハードウェアプリフェッチャが完璧に動作する
        for (size_t i = 0; i < totalBoids; ++i)
        {
            const auto &other = snapshotPtr[i];

            Vector3 diff   = pos - other.position;
            float   distSq = diff.LengthSquared();

            // 自分自身との比較(distSq == 0)は除外
            if (distSq > 0.0001f && distSq < radiusSq)
            {
                // 1. Separation (分離)
                Vector3 repel = diff;
                repel.Normalize();
                repel /= std::sqrt(distSq); // 距離に反比例した強い力
                separation += repel;

                // 2. Alignment (整列)
                alignment += other.velocity;

                // 3. Cohesion (結合)
                cohesion += other.position;

                neighborCount++;
            }
        }

        Vector3 steeringForce = Vector3::Zero;

        if (neighborCount > 0)
        {
            alignment /= (float)neighborCount;
            cohesion /= (float)neighborCount;

            alignment.Normalize();
            alignment *= settings.maxSpeed;
            Vector3 alignSteer = alignment - vel;

            Vector3 targetDir = cohesion - pos;
            targetDir.Normalize();
            targetDir *= settings.maxSpeed;
            Vector3 cohesionSteer = targetDir - vel;

            steeringForce += separation * settings.separationWeight;
            steeringForce += alignSteer * settings.alignmentWeight;
            steeringForce += cohesionSteer * settings.cohesionWeight;
        }

        // =======================================================
        // 4. Bounds (境界引力) - 空間の外へ逃げないように引き戻す
        // =======================================================
        Vector3 toCenter = settings.boundsCenter - pos;
        if (toCenter.LengthSquared() > (settings.boundsRadius * settings.boundsRadius))
        {
            toCenter.Normalize();
            toCenter *= settings.maxSpeed;
            Vector3 boundSteer = toCenter - vel;
            steeringForce += boundSteer * settings.boundsWeight;
        }

        // 力を制限（滑らかな旋回を保証する）
        if (steeringForce.LengthSquared() > (settings.maxForce * settings.maxForce))
        {
            steeringForce.Normalize();
            steeringForce *= settings.maxForce;
        }

        // 速度の更新 (scaledDt を使用)
        boid.velocity += steeringForce * scaledDt;

        if (boid.velocity.LengthSquared() > (settings.maxSpeed * settings.maxSpeed))
        {
            boid.velocity.Normalize();
            boid.velocity *= settings.maxSpeed;
        }

        // =======================================================
        // Transformの更新（位置と回転）
        // =======================================================
        trans.position = trans.position + boid.velocity * scaledDt;

        if (boid.velocity.LengthSquared() > 0.001f)
        {
            Vector3 forward = boid.velocity;
            forward.Normalize();
            Vector3 up = Vector3::Up;

            if (std::abs(forward.Dot(up)) > 0.99f)
            {
                up = Vector3::Right;
            }

            Matrix lookAt  = Matrix::CreateLookAt(Vector3::Zero, forward, up);
            trans.rotation = Quaternion::CreateFromRotationMatrix(lookAt.Invert());
        }

        trans.isStatic = false;
        trans.isDirty  = true;
    });
}

// -------------------------------------------------------------
// GUIでのリアルタイム編集
// -------------------------------------------------------------
void BoidSystem::OnGui()
{
    if (ImGui::CollapsingHeader("Boids Swarm Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Flock Rules");
        ImGui::SliderFloat("Separation (分離)", &m_settings.separationWeight, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Alignment (整列)", &m_settings.alignmentWeight, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Cohesion (結合)", &m_settings.cohesionWeight, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Neighbor Radius", &m_settings.neighborRadius, 1.0f, 20.0f, "%.1f");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Movement Limits");
        ImGui::SliderFloat("Max Speed", &m_settings.maxSpeed, 1.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Max Turn Force", &m_settings.maxForce, 1.0f, 20.0f, "%.1f");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Bounding Area");
        ImGui::SliderFloat("Bounds Radius", &m_settings.boundsRadius, 10.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Bounds Weight", &m_settings.boundsWeight, 0.0f, 10.0f, "%.2f");

        ImGui::Unindent();
    }
}

REGISTER_LOGIC_SYSTEM(BoidSystem, Priority::LogicStage::L02_Update);