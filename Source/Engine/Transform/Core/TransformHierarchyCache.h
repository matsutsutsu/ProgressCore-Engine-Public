#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::ECS { class World; }

/**
 * @brief 非ルート（parent!=0）の Transform だけを depth 順に並べたトポロジキャッシュ。
 * @details ルート（parent==0）は含めない。ルートは並列インプレースで計算され
 *          worldMatrix に直接書かれる。本キャッシュは「親worldを継承する子孫」を
 *          親→子の順で解決するための並び順と参照情報のみを持つ。
 *          フラットシーンでは Count()==0 となり、階層解決コストはゼロ。
 */
class TransformHierarchyCache {
public:
    void Rebuild(CCL::ECS::World& world);

    int OrderOf(CCL::ECS::EntityID id) const {
        const uint32_t pure = CCL::ECS::EntityHandle::GetIndex(id);
        return (pure < m_idToOrder.size()) ? m_idToOrder[pure] : -1;
    }
    size_t              Count()              const { return m_order.size(); }
    CCL::ECS::EntityID  EntityAt(int i)      const { return m_order[i]; }
    int                 ParentInOrderOf(int i) const { return m_parentInOrder[i]; } ///< -1=親はルート
    CCL::ECS::EntityID  ParentIdOf(int i)    const { return m_parentId[i]; }        ///< 親EntityID（常時）

    // --- solve 用スクラッチ（dense） ---
    void StoreWorld(int i, DirectX::FXMMATRIX m) { DirectX::XMStoreFloat4x4(&m_worldScratch[i], m); }
    DirectX::XMMATRIX WorldAt(int i)       const { return DirectX::XMLoadFloat4x4(&m_worldScratch[i]); }

private:
    std::vector<CCL::ECS::EntityID>  m_order;          ///< 非ルートのみ（親が必ず前に来る）
    std::vector<int>                 m_parentInOrder;   ///< 親の order index（親がルートなら -1）
    std::vector<CCL::ECS::EntityID>  m_parentId;        ///< 親 EntityID（ルート親 fetch & 変化検出）
    std::vector<int>                 m_idToOrder;       ///< pure index -> order（-1=非登録）
    std::vector<DirectX::XMFLOAT4X4> m_worldScratch;    ///< solve 中間結果（子が親を読む）
};