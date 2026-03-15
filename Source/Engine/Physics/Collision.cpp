#include "Collision.h"
#include "Engine/Physics/BvhConfig.h"
#include <DirectXCollision.h>
#include <cmath> // std::isnan を使うために必要

#include "Engine/Graphics/Resource/Model.h"

using namespace DirectX;

// シンプルかつ確実な判定関数
inline bool IsBadPtr(const void *ptr) {
    if (ptr == nullptr)
        return true;
    // -1 (0xFFFFFFFFFFFFFFFF) は確実に不正なので弾く
    if (ptr == reinterpret_cast<const void *>(0xFFFFFFFFFFFFFFFF))
        return true;
    return false;
}


bool Collision::RayCast(const DirectX::XMFLOAT3 &start,
    const DirectX::XMFLOAT3                     &end,
    const DirectX::XMFLOAT4X4                   &worldTransform,
    const Model                                 *model,
    DirectX::XMFLOAT3                           &hitPosition,
    DirectX::XMFLOAT3                           &hitNormal) {

    // 1. Modelポインタのチェック
    if (IsBadPtr(model))
        return false;

    // 1. AABBによる早期リターン
    XMVECTOR WorldRayStart = XMLoadFloat3(&start);
    XMVECTOR WorldRayEnd   = XMLoadFloat3(&end);

    // --- そもそも入力が腐っていないかチェック ---
    if (XMVector3IsNaN(WorldRayStart) || XMVector3IsNaN(WorldRayEnd)) {
        return false;
    }

    XMVECTOR WorldRayVec   = XMVectorSubtract(WorldRayEnd, WorldRayStart);
    float    rayDistance   = XMVectorGetX(XMVector3Length(WorldRayVec));

    // NaN または ほぼゼロ の場合を弾く
    // rayDistance != rayDistance は NaN判定のテクニックですが、DirectXMathなら IsNaN 推奨
    if (std::isnan(rayDistance) || rayDistance <= 0.0001f) return false;

    XMVECTOR WorldRayDir = XMVector3Normalize(WorldRayVec);

    const ModelResource *resource  = model->GetResource();
    // ★ここが重要: 取得した resource が -1 だったら即座に帰る
    if (IsBadPtr(resource))
        return false;

    BoundingBox          worldAABB = resource->GetBoundingBox();

    XMMATRIX ParentWorldTransform = XMLoadFloat4x4(&worldTransform);
    worldAABB.Transform(worldAABB, ParentWorldTransform);
    float dist = 0.0f;
    if (!worldAABB.Intersects(WorldRayStart, WorldRayDir, dist) || dist > rayDistance)
        return false;

    // ----------------------------------------------------

    bool  globalHit         = false;
    float nearestGlobalDist = rayDistance;

    // 2. メッシュごとのBVH判定
    for (const ModelResource::Mesh &mesh : resource->GetMeshes()) {
        // BVHが構築されていなければスキップ
        if (mesh.bvh.bvhNode == nullptr)
            continue;

        // ワールド行列計算
        const Model::Node &node            = model->GetNodes().at(mesh.nodeIndex);
        XMMATRIX           GlobalTransform = XMLoadFloat4x4(&node.globalTransform);
        XMMATRIX           WorldTransform = XMMatrixMultiply(GlobalTransform, ParentWorldTransform);
        XMMATRIX           InverseWorldTransform = XMMatrixInverse(nullptr, WorldTransform);

        // レイをローカル空間へ変換
        XMVECTOR LocalRayStart = XMVector3TransformCoord(WorldRayStart, InverseWorldTransform);
        XMVECTOR LocalRayEnd   = XMVector3TransformCoord(WorldRayEnd, InverseWorldTransform);
        XMVECTOR LocalRayVec   = LocalRayEnd - LocalRayStart;
        float    localLen      = XMVectorGetX(XMVector3Length(LocalRayVec));

        if (localLen <= 0.00001f)
            continue;
        XMVECTOR LocalRayDir = XMVector3Normalize(LocalRayVec);

        XMFLOAT3 fStart, fDir;
        XMStoreFloat3(&fStart, LocalRayStart);
        XMStoreFloat3(&fDir, LocalRayDir);

        // tinybvh用のレイ作成
        // tinybvh::Ray は (origin, direction, t_min = 0, t_max = len) を取ります
        tinybvh::Ray ray(MyBvh::ToVec3(fStart), MyBvh::ToVec3(fDir), localLen);

        // トラバース実行
        // Intersect はヒットした場合、ray.hit.t を更新します
        int steps = mesh.bvh.Intersect(ray);

        // ray.hit.t が更新されていればヒット
        if (ray.hit.t < localLen) {
            // ローカルヒット点
            XMVECTOR LocalHitPos = LocalRayStart + LocalRayDir * ray.hit.t;
            XMVECTOR WorldHitPos = XMVector3TransformCoord(LocalHitPos, WorldTransform);

            float worldDist = XMVectorGetX(XMVector3Length(WorldHitPos - WorldRayStart));

            if (worldDist < nearestGlobalDist) {
                nearestGlobalDist = worldDist;
                globalHit         = true;
                XMStoreFloat3(&hitPosition, WorldHitPos);

                // 法線計算
                // tinybvh は ray.hit.prim にヒットした三角形ID (インデックス配列上のオフセット)
                // を入れます
                uint32_t triIdx = ray.hit.prim;

                // ★修正: 元のインデックス配列 (mesh.indices) を参照する
                uint32_t i0 = mesh.indices[triIdx * 3 + 0];
                uint32_t i1 = mesh.indices[triIdx * 3 + 1];
                uint32_t i2 = mesh.indices[triIdx * 3 + 2];

                XMVECTOR v0 = XMLoadFloat3(&mesh.vertices[i0].position);
                XMVECTOR v1 = XMLoadFloat3(&mesh.vertices[i1].position);
                XMVECTOR v2 = XMLoadFloat3(&mesh.vertices[i2].position);

                // 面法線を計算
                XMVECTOR e1          = v1 - v0;
                XMVECTOR e2          = v2 - v0;
                XMVECTOR localNormal = XMVector3Cross(e1, e2);
                localNormal          = XMVector3Normalize(localNormal);

                // ワールド空間へ変換 (方向ベクトルなので TransformNormal)
                XMVECTOR worldNormal = XMVector3TransformNormal(localNormal, WorldTransform);
                XMStoreFloat3(&hitNormal, XMVector3Normalize(worldNormal));
            }
        }
    }

    return globalHit;
}