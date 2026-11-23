#pragma once
// メッシュ単位の視錐台カリングと、メッシュのワールド行列を求める関数。状態もメモリの確保も持たない
// OpaquePass と NormalPrepass が同じ基準で判定するよう1か所にまとめる。ずれると片方のパスだけで描かれない
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"

namespace MeshCull
{

    // boundsMin / boundsMax から、メッシュのローカルの AABB を作る（mesh.data が null でないことは呼ぶ側で確かめる）
    // 頂点がないメッシュはとても大きな箱になり、常に見える扱いになる（描き漏れない側に倒す）
    inline DirectX::BoundingBox BuildLocalAABB(const Model::Mesh &mesh)
    {
        using namespace DirectX;
        const XMVECTOR bmin = XMLoadFloat3(&mesh.data->boundsMin);
        const XMVECTOR bmax = XMLoadFloat3(&mesh.data->boundsMax);
        BoundingBox    box;
        XMStoreFloat3(&box.Center, XMVectorScale(XMVectorAdd(bmin, bmax), 0.5f));
        XMStoreFloat3(&box.Extents, XMVectorAbs(XMVectorScale(XMVectorSubtract(bmax, bmin), 0.5f)));
        return box;
    }

    // メッシュのワールド行列。ノードがあれば node->globalTransform × entityWorld、なければ entityWorld
    // 描くときとカリングするときで、掛ける順をこれでそろえる
    inline DirectX::XMFLOAT4X4 ComputeMeshWorld(const Model::Mesh         &mesh,
                                                const DirectX::XMFLOAT4X4 &entityWorld)
    {
        DirectX::XMFLOAT4X4 out;
        if (mesh.node)
        {
            const DirectX::XMMATRIX ml = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
            const DirectX::XMMATRIX ew = DirectX::XMLoadFloat4x4(&entityWorld);
            DirectX::XMStoreFloat4x4(&out, ml * ew);
        }
        else
        {
            out = entityWorld;
        }
        return out;
    }

    // ローカルの AABB を world で変換し、視錐台と少しでも重なれば true
    // 回転した箱を包む軸平行の箱で比べるので、実際より大きめに判定する
    inline bool IsMeshVisible(const DirectX::BoundingBox     &localAABB,
                              const DirectX::XMFLOAT4X4      &world,
                              const DirectX::BoundingFrustum &frustum)
    {
        DirectX::BoundingBox worldBox;
        localAABB.Transform(worldBox, DirectX::XMLoadFloat4x4(&world));
        return frustum.Contains(worldBox) != DirectX::DISJOINT;
    }

}
