// 描画パス間で共有するメッシュ単位カリング / ワールド変換ヘルパ。
//
// 役割      : OpaquePass と NormalPrepass が「まったく同じ基準」でメッシュを
//             カリング・ワールド変換することを型レベルで保証する。
// 責務      : ローカル AABB 構築 / メッシュワールド行列合成 / フラスタム交差判定。
// 位置付け  : 各 RenderPass の Execute から呼ばれる純粋関数群（状態を持たない）。
// 設計方針  : 判定ロジックを 1 箇所に集約することで、パス間の実装ズレ
//             （＝片方だけ描き漏れ / 描き過ぎ）という再発しやすいバグを構造的に防ぐ。
//             ヘッダオンリー inline。ホットパスからの呼び出しを想定し確保は行わない。
#pragma once
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"

namespace MeshCull
{

    // メッシュのローカル空間 AABB を boundsMin/Max から構築する。
    //
    // 縮退メッシュ（頂点0）は extents が巨大化し「常に可視」＝安全側（描き漏れ防止）に倒れる。
    //
    // 注意: mesh.data が非 null であることは呼び出し側が保証すること。
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

    // メッシュのワールド行列を求める。node があれば node->globalTransform * entityWorld。
    //
    // 描画時とカリング時でこの合成順を一致させることが座標系整合の前提。
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
 * @brief ローカル AABB を world で変換し、frustum と交差するか判定する。
 * @note  Transform は回転を含んでも「包含する軸平行ボックス」を返すため保守的で正しい。
 * @return 視錐台と少しでも交差していれば true（DISJOINT のときのみ false）。
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
