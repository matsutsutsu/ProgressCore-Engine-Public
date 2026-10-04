#pragma once
// ===========================================================================
// 概要: 親を持つ Transform だけを、親が必ず先に来る順に並べたキャッシュ。
//
// 責務:
//   - 親子関係が変わったときに並び順を作り直す（Rebuild）
//   - 解決中の中間結果（親のワールド行列）を置く作業領域の提供
//
// 備考: TransformUpdateSystem が所有し、World リソースとして
//       TransformInterpolationSystem にも並び順だけを読ませる。
// ===========================================================================
#include <vector>
#include <DirectXMath.h>
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::ECS
{
    class World;
}

/**
 * @brief 非ルート（parent != 0）の Transform だけを、親が先に来る順に並べたキャッシュ。
 *
 * ルート（parent == 0）は含めない。ルートは並列にその場で計算され、worldMatrix に
 * 直接書かれる。このキャッシュは「親のワールド行列を引き継ぐ子孫」を
 * 親→子の順で解決するための並び順と参照情報だけを持つ。
 * 親子の無いシーンでは Count() == 0 になり、階層解決のコストは0。
 */
class TransformHierarchyCache
{
public:
    // World の全 Transform から、非ルートを親が先の順に並べ直す
    void Rebuild(CCL::ECS::World &world);

    // id が並びの何番目かを返す。ルート・未登録なら -1
    int OrderOf(CCL::ECS::EntityID id) const
    {
        const uint32_t pure = CCL::ECS::EntityHandle::GetIndex(id);
        return (pure < m_idToOrder.size()) ? m_idToOrder[pure] : -1;
    }
    size_t             Count() const { return m_order.size(); }
    CCL::ECS::EntityID EntityAt(int i) const { return m_order[i]; }
    int                ParentInOrderOf(int i) const { return m_parentInOrder[i]; } // -1 = 親はルート
    CCL::ECS::EntityID ParentIdOf(int i) const { return m_parentId[i]; }           // 親の EntityID（常に有効）

    // 解決中の中間結果（i 番目のワールド行列）。子が親の値を読むために使う
    void              StoreWorld(int i, DirectX::FXMMATRIX m) { DirectX::XMStoreFloat4x4(&m_worldScratch[i], m); }
    DirectX::XMMATRIX WorldAt(int i) const { return DirectX::XMLoadFloat4x4(&m_worldScratch[i]); }

private:
    std::vector<CCL::ECS::EntityID>  m_order;         // 非ルートのみ（親が必ず前に来る）
    std::vector<int>                 m_parentInOrder; // 親の並び順の位置（親がルートなら -1）
    std::vector<CCL::ECS::EntityID>  m_parentId;      // 親の EntityID（ルート親の取得と、親の変化検出に使う）
    std::vector<int>                 m_idToOrder;     // EntityID の index → 並び順の位置（-1 = 未登録）
    std::vector<DirectX::XMFLOAT4X4> m_worldScratch;  // 解決中の中間結果（子が親の値を読む）
};