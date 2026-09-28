#pragma once
// ===========================================================================
// 概要: メッシュ単位の視錐台カリングと、メッシュのワールド行列を求める関数。
//
// 責務:
//   - メッシュのローカル AABB を作る
//   - ノードの行列とエンティティのワールド行列から、メッシュのワールド行列を求める
//   - AABB が視錐台と重なるかを判定する
//
// 備考: OpaquePass と NormalPrepass が同じ基準でカリングするよう、判定を1か所にまとめている。
//       基準がずれると、片方のパスだけで描かれない・余計に描かれるメッシュが出る。
//       状態を持たない inline 関数で、メモリを確保しない。
// ===========================================================================
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"

namespace MeshCull
{

    /**
     * @brief メッシュのローカル空間の AABB を boundsMin / boundsMax から作る。
     *
     * 頂点が無いメッシュは extents がとても大きくなり、常に見える扱いになる（描き漏れない側に倒れる）。
     *
     * @note mesh.data が null でないことは呼び出し側で確かめる。
     */
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

    // メッシュのワールド行列。ノードがあれば node->globalTransform × entityWorld。
    // 描くときとカリングするときで、この掛ける順をそろえる
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

    /**
     * @brief ローカルの AABB を world で変換し、視錐台と重なるかを判定する。
     * @note BoundingBox::Transform は、回転した箱を包む軸平行の箱を返すので、実際より大きめに判定する。
     * @return 少しでも重なれば true（DISJOINT のときだけ false）
     */
    inline bool IsMeshVisible(const DirectX::BoundingBox     &localAABB,
                              const DirectX::XMFLOAT4X4      &world,
                              const DirectX::BoundingFrustum &frustum)
    {
        DirectX::BoundingBox worldBox;
        localAABB.Transform(worldBox, DirectX::XMLoadFloat4x4(&world));
        return frustum.Contains(worldBox) != DirectX::DISJOINT;
    }

} // namespace MeshCull
