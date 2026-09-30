#pragma once
// ===========================================================================
// 概要: BT の距離条件から「間合いのリング」を集める規則と、その色（エディタのデバッグ表示用）。
//
// 責務:
//   - 読み込み済みの木から距離条件の param を集め、同じ半径を1本にまとめる
//   - 内側で成立 / 外側で成立 / 両方 / ステージ端 の色を決める
//   - 今の距離がどの帯（しきい値としきい値の間）に入っているかを引く
//
// 備考:
//   AIDebugSpatialSystem（地面のリングと帯）と AIDebugOverlaySystem（距離の数字の色）の
//   両方が使う。規則と色を片方ずつに持つと、片方だけ直したときに「地面の帯は赤なのに
//   数字は青」のように食い違うので、ここに1つだけ置く。
// ===========================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <DirectXMath.h>

#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

namespace AIDebugRanges
{
    // どちら向きの条件がこの半径を使っているか。同じ半径を両方が使うこともある
    // （近接の枝の上限と接近の枝の下限が同じ値なら、そこが間合いの境目）
    enum RangeKind : uint8_t
    {
        kInside  = 1 << 0, // C_DistanceLessThan    … この内側で成立
        kOutside = 1 << 1, // C_DistanceGreaterThan … この外側で成立
    };

    struct RangeRing
    {
        float   radius = 0.0f;
        uint8_t kinds  = 0; // RangeKind のビット和
    };

    inline const DirectX::XMFLOAT4 kColInside(1.0f, 0.35f, 0.20f, 1.0f);  // 内側で成立：赤オレンジ
    inline const DirectX::XMFLOAT4 kColOutside(0.20f, 0.70f, 1.0f, 1.0f); // 外側で成立：シアン
    inline const DirectX::XMFLOAT4 kColBoth(0.45f, 1.0f, 0.55f, 1.0f);    // 両方が使う境目：緑
    inline const DirectX::XMFLOAT4 kColEdge(0.80f, 0.55f, 1.0f, 1.0f);    // ステージ端（ステージの中心から）：紫

    /// リングの色。帯の色も、その帯の外側の境界線の色に合わせる（線と塗りで意味を食い違わせない）。
    inline const DirectX::XMFLOAT4 &ColorOf(const RangeRing &ring)
    {
        const bool inside  = (ring.kinds & kInside) != 0;
        const bool outside = (ring.kinds & kOutside) != 0;
        return (inside && outside) ? kColBoth : inside ? kColInside : kColOutside;
    }

    /// 同じ半径をまとめるためのキー。JSON から読んだ値をそのまま float で比べると、
    /// 見た目が同じ 25.0 でも別リングとして二重に描かれることがある。
    inline int RadiusKey(float radius) { return static_cast<int>(std::lroundf(radius * 100.0f)); }

    /// 木を走査して距離条件の param を集める。同じ半径は1本にまとめ、内側から順に並べる。
    ///   rings     … ボス中心のリング（C_DistanceLessThan / C_DistanceGreaterThan）
    ///   edgeRings … ステージ端の条件（C_IsAtStageEdge）。持つのは端からの幅で、半径ではない
    inline void CollectRings(const BTAsset &asset, std::vector<RangeRing> &rings, std::vector<float> &edgeRings)
    {
        rings.clear();
        edgeRings.clear();

        for (const BTNode &node : asset.nodes)
        {
            if (node.type != BTNodeType::Condition) continue;

            // ステージ端だけは、中心がボスではなくステージの中心。同じ距離の条件でも
            // ボス中心で描くと実際の判定と違うので、集める配列を分ける。param は端からの幅なので
            // 0 も「壁際」という有効な値（下の未設定の判定より先に拾う）。
            if (node.actionOrConditionId == C_IsAtStageEdge)
            {
                const int  key   = RadiusKey(node.param);
                const bool known = std::any_of(edgeRings.begin(), edgeRings.end(),
                                               [key](float r) { return RadiusKey(r) == key; });
                if (!known) edgeRings.push_back(node.param);
                continue;
            }

            if (node.param <= 0.0f) continue; // 未設定のノードは線にしない

            uint8_t kind = 0;
            if (node.actionOrConditionId == C_DistanceLessThan)
                kind = kInside;
            else if (node.actionOrConditionId == C_DistanceGreaterThan)
                kind = kOutside;
            else
                continue;

            // 同じ半径は1本にまとめ、どちら向きに使われているかだけを足していく。
            const int key   = RadiusKey(node.param);
            auto      found = std::find_if(rings.begin(), rings.end(),
                                           [key](const RangeRing &r) { return RadiusKey(r.radius) == key; });
            if (found != rings.end())
                found->kinds |= kind;
            else
                rings.push_back({node.param, kind});
        }

        // 内側から順に並べる。GUIの一覧を間合いの順に読めるようにするため。
        std::sort(rings.begin(), rings.end(), [](const RangeRing &a, const RangeRing &b) { return a.radius < b.radius; });
        std::sort(edgeRings.begin(), edgeRings.end());
    }

    /// distance が入っている帯の外側の境界（＝その帯の色を決めるリング）。
    /// どのリングよりも外なら nullptr。rings は CollectRings で内側から並べたものを渡す。
    inline const RangeRing *BandOf(const std::vector<RangeRing> &rings, float distance)
    {
        for (const RangeRing &ring : rings)
            if (distance < ring.radius) return &ring;
        return nullptr;
    }
} // namespace AIDebugRanges
