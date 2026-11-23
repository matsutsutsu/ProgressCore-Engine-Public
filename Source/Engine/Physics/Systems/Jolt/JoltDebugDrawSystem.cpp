#include "JoltDebugDrawSystem.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    // クォータニオンからオイラー角（ラジアン）へ変換する（ShapeRenderer::DrawBox 用）
    DirectX::XMFLOAT3 QuatToEulerRadian(const DirectX::XMFLOAT4 &q)
    {
        float pitch = std::asin(std::clamp(2.0f * (q.w * q.x - q.y * q.z), -1.0f, 1.0f));
        float yaw   = std::atan2(2.0f * (q.w * q.y + q.z * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        float roll  = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
        return {pitch, yaw, roll};
    }



    // コライダー可視化の色。シーンの色と被らないよう明るい緑にしている
    const DirectX::XMFLOAT4 COLLIDER_COLOR = {0.2f, 1.0f, 0.2f, 1.0f};
}

// ============================================================
// BoxCollider
// ============================================================
void JoltBoxDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const TransformComponent &trans, const JoltBoxColliderComponent &box) {
        DirectX::XMFLOAT3 worldPos     = trans.GetWorldPosition();
        DirectX::XMFLOAT4 worldRotQuat = trans.GetWorldRotation();

        // 1. Entity のワールド行列（剛体の形状にはスケールが掛からないので、位置と回転だけ）
        XMMATRIX worldRot   = XMMatrixRotationQuaternion(XMLoadFloat4(&worldRotQuat));
        XMMATRIX worldTrans = XMMatrixTranslation(worldPos.x, worldPos.y, worldPos.z);
        XMMATRIX worldMat   = worldRot * worldTrans;

        // 2. コライダーのローカルのずれ
        XMMATRIX localRot   = XMMatrixRotationRollPitchYaw(XMConvertToRadians(box.localRotationEuler.x), XMConvertToRadians(box.localRotationEuler.y), XMConvertToRadians(box.localRotationEuler.z));
        XMMATRIX localTrans = XMMatrixTranslation(box.localOffset.x, box.localOffset.y, box.localOffset.z);
        XMMATRIX localMat   = localRot * localTrans;

        // 3. ローカルのずれを適用してから、ワールドに配置する
        XMMATRIX finalMat = localMat * worldMat;

        // 最終的な座標と回転を抽出して描画
        XMVECTOR outScale, outRotQuatV, outTrans;
        XMMatrixDecompose(&outScale, &outRotQuatV, &outTrans, finalMat);

        XMFLOAT3 finalPos, finalEuler;
        XMStoreFloat3(&finalPos, outTrans);

        // DrawBox はオイラー角（ラジアン）で受け取る
        XMFLOAT4 q;
        XMStoreFloat4(&q, outRotQuatV);
        finalEuler = QuatToEulerRadian(q);

        DirectX::XMFLOAT3 size = box.halfExtent;

        renderer->DrawBox(finalPos, finalEuler, size, COLLIDER_COLOR);
    });
}



// ============================================================
// SphereCollider
// ============================================================
void JoltSphereDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const TransformComponent &trans, const JoltSphereColliderComponent &sphere) {
        DirectX::XMFLOAT3 worldPos     = trans.GetWorldPosition();
        DirectX::XMFLOAT4 worldRotQuat = trans.GetWorldRotation();

        XMMATRIX worldRot   = XMMatrixRotationQuaternion(XMLoadFloat4(&worldRotQuat));
        XMMATRIX worldTrans = XMMatrixTranslation(worldPos.x, worldPos.y, worldPos.z);
        XMMATRIX worldMat   = worldRot * worldTrans;

        XMMATRIX localTrans = XMMatrixTranslation(sphere.localOffset.x, sphere.localOffset.y, sphere.localOffset.z);

        XMMATRIX finalMat = localTrans * worldMat;

        XMVECTOR outScale, outRot, outTrans;
        XMMatrixDecompose(&outScale, &outRot, &outTrans, finalMat);
        XMFLOAT3 finalPos;
        XMStoreFloat3(&finalPos, outTrans);

        renderer->DrawSphere(finalPos, sphere.radius, COLLIDER_COLOR);
    });
}



// ============================================================
// CapsuleCollider
// ============================================================
void JoltCapsuleDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const TransformComponent &trans, const JoltCapsuleColliderComponent &capsule) {
        DirectX::XMFLOAT3 worldPos     = trans.GetWorldPosition();
        DirectX::XMFLOAT4 worldRotQuat = trans.GetWorldRotation();

        XMMATRIX worldRot   = XMMatrixRotationQuaternion(XMLoadFloat4(&worldRotQuat));
        XMMATRIX worldTrans = XMMatrixTranslation(worldPos.x, worldPos.y, worldPos.z);
        XMMATRIX worldMat   = worldRot * worldTrans;

        XMMATRIX localRot   = XMMatrixRotationRollPitchYaw(XMConvertToRadians(capsule.localRotationEuler.x), XMConvertToRadians(capsule.localRotationEuler.y), XMConvertToRadians(capsule.localRotationEuler.z));
        XMMATRIX localTrans = XMMatrixTranslation(capsule.localOffset.x, capsule.localOffset.y, capsule.localOffset.z);
        XMMATRIX localMat   = localRot * localTrans;

        XMMATRIX finalMat = localMat * worldMat;

        XMFLOAT4X4 mat;
        XMStoreFloat4x4(&mat, finalMat);

        renderer->DrawCapsule(mat, capsule.radius, capsule.halfHeight * 2.0f, COLLIDER_COLOR);
    });
}



// ============================================================
// MeshCollider
// ============================================================
void JoltMeshDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const TransformComponent        &trans,
                const JoltMeshColliderComponent &meshCol,
                const ModelComponent            &modelComp) {
        // 無効、またはモデルがロードされていない場合はスキップ
        if (!meshCol.isEnabled || !modelComp.GetModel()) return;

        const ModelResource *res = modelComp.GetModel()->GetResource();
        if (!res) return;

        // メッシュの形状にはスケールも掛かるので、合成済みの worldMatrix をそのまま使う
        XMMATRIX worldMat = XMLoadFloat4x4(&trans.worldMatrix);

        for (const auto &mesh : res->GetMeshes())
        {
            // indices は 3つで1つの三角形
            for (size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                uint32_t i0 = mesh.indices[i + 0];
                uint32_t i1 = mesh.indices[i + 1];
                uint32_t i2 = mesh.indices[i + 2];

                XMVECTOR v0 = XMLoadFloat3(&mesh.vertices[i0].position);
                XMVECTOR v1 = XMLoadFloat3(&mesh.vertices[i1].position);
                XMVECTOR v2 = XMLoadFloat3(&mesh.vertices[i2].position);

                v0 = XMVector3Transform(v0, worldMat);
                v1 = XMVector3Transform(v1, worldMat);
                v2 = XMVector3Transform(v2, worldMat);

                XMFLOAT3 p0, p1, p2;
                XMStoreFloat3(&p0, v0);
                XMStoreFloat3(&p1, v1);
                XMStoreFloat3(&p2, v2);

                renderer->DrawTriangle(p0, p1, p2, COLLIDER_COLOR);
            }
        }
    });
}



REGISTER_RENDER_SYSTEM(JoltBoxDebugDrawSystem, Priority::RenderStage::R08_Main);
REGISTER_RENDER_SYSTEM(JoltSphereDebugDrawSystem, Priority::RenderStage::R08_Main);
REGISTER_RENDER_SYSTEM(JoltCapsuleDebugDrawSystem, Priority::RenderStage::R08_Main);
REGISTER_RENDER_SYSTEM(JoltMeshDebugDrawSystem, Priority::RenderStage::R08_Main);