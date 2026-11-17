#pragma once
// ===========================================================================
// 概要: インスタンスごとに LOD の段を選び、見えるインスタンスの列を段の順に並べ替える関数
//
// 責務:
//   - メッシュが画面に映る大きさから、LOD の段を選ぶ
//   - インスタンスの列を LOD0 → LOD1 → … の順に並べ替え、段ごとの数を返す
//   - 段ごとに、描くインデックスバッファと範囲を返す
//
// 備考: NormalPrepass と OpaquePass が同じ段で描くよう、選び方をここ1か所にまとめている
//       段がずれると、プリパスが書いた深度と本描画の形が合わず、ちらつきや穴になる
// ===========================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>
#include <d3d11.h>
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Renderer/Core/InstanceBuffer.h"

namespace MeshLod
{

    constexpr size_t kMaxLevels = 4; // 扱える段の数の上限（今は LOD0〜2 の3段）

    using LevelCounts = std::array<uint32_t, kMaxLevels>; // 段ごとのインスタンスの数（[0] が LOD0）

    // 段を切り替える境目（包む球の直径 ÷ 画面の高さ）
    // [i] 以上なら LOD i。どれにも届かなければ一番粗い段
    constexpr float kLevelScreenRatios[] = {
        0.08f, // 画面の高さの 8% 以上 → LOD0
        0.02f, // 2% 以上 → LOD1、それ未満 → LOD2
    };

    // 段を描くときのインデックスバッファと、その中の範囲
    struct DrawRange
    {
        ID3D11Buffer *indexBuffer = nullptr; // 所有しない
        UINT          startIndex  = 0;       // DrawIndexedInstanced の StartIndexLocation
        UINT          indexCount  = 0;       // DrawIndexedInstanced の IndexCountPerInstance
    };

    // 1つのインスタンスの段を、画面に映る大きさから選ぶ（0 〜 levelCount - 1）。world は MeshCull::ComputeMeshWorld の結果
    inline uint32_t SelectLevel(const DirectX::BoundingBox &localAABB, const DirectX::XMFLOAT4X4 &world,
                                const DirectX::XMFLOAT3 &eye, float tanHalfFovY, uint32_t levelCount)
    {
        using namespace DirectX;

        // 距離ではなく、画面の高さに対する大きさで選ぶ
        // 距離だけだと、ズームで画角を変えたときに見た目の粗さが変わる

        const XMMATRIX m = XMLoadFloat4x4(&world);

        // 包む球の半径 = ローカルの箱の対角の半分 × 一番大きい軸のスケール
        const float scale  = (std::max)({XMVectorGetX(XMVector3Length(m.r[0])),
                                         XMVectorGetX(XMVector3Length(m.r[1])),
                                         XMVectorGetX(XMVector3Length(m.r[2]))});
        const float radius = XMVectorGetX(XMVector3Length(XMLoadFloat3(&localAABB.Extents))) * scale;

        const XMVECTOR center   = XMVector3Transform(XMLoadFloat3(&localAABB.Center), m);
        const float    distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(center, XMLoadFloat3(&eye))));

        // カメラが球の中に入っているときは、一番細かい段にする
        if (distance <= radius) return 0;

        // 直径 ÷ 画面の高さ = (2 × 半径) ÷ (2 × 距離 × tan(画角 / 2))
        const float ratio = radius / (distance * tanHalfFovY);

        uint32_t level = 0;
        while (level < std::size(kLevelScreenRatios) && ratio < kLevelScreenRatios[level])
        {
            ++level;
        }
        return (std::min)(level, levelCount - 1);
    }

    // instances を段の順に並べ替え、段ごとの数を返す。temp は作業用（instances と入れ替えて容量を使い回す）
    // LOD がないメッシュと、正射影（fovY が 0）のカメラでは並べ替えず、すべて LOD0 にする
    inline LevelCounts SortInstancesByLevel(std::vector<InstanceData> &instances, std::vector<InstanceData> &temp,
                                           const Model::Mesh &mesh, const DirectX::BoundingBox &localAABB,
                                           const DirectX::XMFLOAT3 &eye, float fovY)
    {
        // 処理フロー:
        // 1. 段ごとの数を数える
        // 2. 段ごとの置き場所の先頭を決め、そこへ詰める

        // 比べて並べ替えず、数えて置き場所を決めるので、数に比例した時間で済む
        // 段は 1 と 2 で2回求める。段の配列を別に持たずに済み、1000 個程度なら十分に軽い

        LevelCounts counts{};

        const uint32_t levelCount = static_cast<uint32_t>((std::min)(mesh.data->lods.size(), kMaxLevels));
        if (levelCount <= 1 || fovY <= 0.0f)
        {
            counts[0] = static_cast<uint32_t>(instances.size());
            return counts;
        }

        const float tanHalfFovY = std::tan(fovY * 0.5f);

        // ---------------------------------------------------------
        // 1. 段ごとの数
        // ---------------------------------------------------------
        for (const InstanceData &instance : instances)
        {
            ++counts[SelectLevel(localAABB, instance.world, eye, tanHalfFovY, levelCount)];
        }

        // ---------------------------------------------------------
        // 2. 置き場所へ詰める
        // ---------------------------------------------------------
        std::array<uint32_t, kMaxLevels> writePos{}; // 段ごとの、次に書く temp の添字
        for (size_t level = 1; level < kMaxLevels; ++level)
        {
            writePos[level] = writePos[level - 1] + counts[level - 1];
        }

        temp.resize(instances.size());
        for (const InstanceData &instance : instances)
        {
            const uint32_t level     = SelectLevel(localAABB, instance.world, eye, tanHalfFovY, levelCount);
            temp[writePos[level]++] = instance;
        }
        instances.swap(temp);

        return counts;
    }

    // 全部を LOD0 として数える（スキンメッシュなど、段を選ばないとき）
    inline LevelCounts AllInLevel0(size_t count)
    {
        LevelCounts counts{};
        counts[0] = static_cast<uint32_t>(count);
        return counts;
    }

    // 段 level を描くときのインデックスバッファと範囲。LOD がないメッシュは、どの段でも元の indexBuffer の全体
    inline DrawRange GetDrawRange(const ModelResource::Mesh &data, size_t level)
    {
        if (data.lods.empty())
        {
            return {data.indexBuffer.Get(), 0, static_cast<UINT>(data.indices.size())};
        }

        const ModelResource::LodLevel &lod = data.lods[(std::min)(level, data.lods.size() - 1)];
        return {data.lodIndexBuffer.Get(), lod.startIndex, lod.indexCount};
    }

    // 一番粗い段の範囲。影のように細部が結果に出ない描画に使う。LOD がないメッシュは元の indexBuffer の全体
    inline DrawRange GetCoarsestDrawRange(const ModelResource::Mesh &data)
    {
        if (data.lods.empty()) return GetDrawRange(data, 0);
        return GetDrawRange(data, data.lods.size() - 1);
    }

}
