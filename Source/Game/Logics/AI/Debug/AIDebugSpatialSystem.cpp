// AIDebugSpatialSystem の実装。ShapeRenderer で間合いリングと照準線を描く。
#include "AIDebugSpatialSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
    // 地面(XZ平面)へ寝かせるための回転。円は XY 平面で定義されるので X軸+90°。
    const XMFLOAT4 kGroundQuat(0.70710678f, 0.0f, 0.0f, 0.70710678f);

    const XMFLOAT4 kColInside(1.0f, 0.35f, 0.20f, 1.0f);  // 内側で成立：赤オレンジ
    const XMFLOAT4 kColOutside(0.20f, 0.70f, 1.0f, 1.0f); // 外側で成立：シアン
    const XMFLOAT4 kColBoth(0.45f, 1.0f, 0.55f, 1.0f);    // 両方が使う境目：緑
    const XMFLOAT4 kColEdge(0.80f, 0.55f, 1.0f, 1.0f);    // ステージ端（原点中心）：紫
    const XMFLOAT4 kColNow(1.0f, 0.35f, 1.0f, 1.0f);      // 今の間合い：マゼンタ
    const XMFLOAT4 kColLine(1.0f, 0.90f, 0.20f, 1.0f);    // 照準線：黄

    // 同じ半径をまとめるためのキー。JSONから来た値をそのまま float 比較すると、
    // 見た目が同じ 25.0 でも別リングとして二重に描かれることがある。
    int RadiusKey(float radius) { return static_cast<int>(std::lroundf(radius * 100.0f)); }

    // これより低ければ壁にせず地面の円で描く。
    constexpr float kFlatRingThreshold = 0.05f;
}


void AIDebugSpatialSystem::DrawRangeRing(ShapeRenderer *renderer, const XMFLOAT3 &footCenter,
                                         float radius, const XMFLOAT4 &color) const
{
    if (m_ringHeight <= kFlatRingThreshold)
    {
        renderer->DrawWireframeCircle(footCenter, radius, color, kGroundQuat);
        return;
    }

    // シリンダーは中心を渡す仕様なので、足元から立ち上がるよう半分だけ持ち上げる。
    const XMFLOAT3 mid(footCenter.x, footCenter.y + m_ringHeight * 0.5f, footCenter.z);
    renderer->DrawWireframeCylinder(mid, radius, m_ringHeight, color);
}


std::vector<CCL::ECS::TypeID> AIDebugSpatialSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(CCL::ECS::TypeInfo<BehaviorTreeComponent>::ID());
    return types;
}


void AIDebugSpatialSystem::CollectRings(const BTAsset &asset)
{
    m_rings.clear();
    m_edgeRings.clear();

    for (const BTNode &node : asset.nodes)
    {
        if (node.type != BTNodeType::Condition) continue;
        if (node.param <= 0.0f) continue;   // 未設定のノードは線にしない

        // ステージ端だけは中心がボスではなくワールド原点。同じ「距離を持つ条件」でも
        // ボス中心で描くと嘘になるので、集める先から分ける。
        if (node.actionOrConditionId == ::CCL::AI::C_IsAtStageEdge)
        {
            const int key = RadiusKey(node.param);
            const bool known = std::any_of(m_edgeRings.begin(), m_edgeRings.end(),
                                           [key](float r) { return RadiusKey(r) == key; });
            if (!known) m_edgeRings.push_back(node.param);
            continue;
        }

        uint8_t kind = 0;
        if (node.actionOrConditionId == ::CCL::AI::C_DistanceLessThan)         kind = kInside;
        else if (node.actionOrConditionId == ::CCL::AI::C_DistanceGreaterThan) kind = kOutside;
        else continue;

        // 同じ半径は1本にまとめ、どちら向きに使われているかだけを足していく。
        const int key   = RadiusKey(node.param);
        auto      found = std::find_if(m_rings.begin(), m_rings.end(),
                                       [key](const RangeRing &r) { return RadiusKey(r.radius) == key; });
        if (found != m_rings.end()) found->kinds |= kind;
        else                        m_rings.push_back({node.param, kind});
    }

    // 内側から順に並べる。GUIの一覧を間合いの順に読めるようにするため。
    std::sort(m_rings.begin(), m_rings.end(),
              [](const RangeRing &a, const RangeRing &b) { return a.radius < b.radius; });
    std::sort(m_edgeRings.begin(), m_edgeRings.end());
}


void AIDebugSpatialSystem::Update(float /*dt*/)
{
    if (!m_enabled) return;

    // ShapeRenderer が無ければ描けない（NavMesh 等と同じ前提）。
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    // プレイヤー座標は BattleContext（世界の事実）から。無ければ照準線だけ省く。
    XMFLOAT3 playerPos(0, 0, 0);
    bool     hasPlayer = false;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        playerPos = XMFLOAT3(battle.playerPos.x, battle.playerPos.y, battle.playerPos.z);
        hasPlayer = battle.HasPlayer();
    }

    ForEachWithID([&](EntityID entity,
                      const TransformComponent &trans,
                      const BossActionComponent & /*boss*/) {
        // 地面にわずかに浮かせて Z ファイティングを避ける。
        const XMFLOAT3 center(trans.position.x, trans.position.y + 0.05f, trans.position.z);

        const auto *bt = m_world->GetComponent<BehaviorTreeComponent>(entity);
        if (bt && bt->sharedAsset) CollectRings(*bt->sharedAsset);
        else                       { m_rings.clear(); m_edgeRings.clear(); }

        for (const RangeRing &ring : m_rings)
        {
            const bool inside  = (ring.kinds & kInside)  != 0;
            const bool outside = (ring.kinds & kOutside) != 0;
            const XMFLOAT4 &col = (inside && outside) ? kColBoth
                                : inside              ? kColInside
                                                      : kColOutside;
            DrawRangeRing(renderer, center, ring.radius, col);
        }

        // 今の間合い。しきい値のどの帯に居るのかが、線を数えなくても分かる。
        //   しきい値のリングと重なっても潰れないよう、少し高い位置へ描く。
        if (m_drawNow && bt)
        {
            const float now = bt->blackboard.distanceToTarget;
            if (now > 0.01f)
            {
                const XMFLOAT3 raised(center.x, center.y + 0.05f, center.z);
                DrawRangeRing(renderer, raised, now, kColNow);
            }
        }

        if (m_drawLine && hasPlayer)
        {
            const XMFLOAT3 pTarget(playerPos.x, playerPos.y + 0.05f, playerPos.z);
            renderer->DrawArrow(center, pTarget, 0.5f, kColLine);
        }

        // ステージ端は原点中心。ボスの足元の高さに合わせておく。
        for (float radius : m_edgeRings)
        {
            const XMFLOAT3 origin(0.0f, center.y, 0.0f);
            DrawRangeRing(renderer, origin, radius, kColEdge);
        }
    });
}


void AIDebugSpatialSystem::OnGui()
{
    // 別ウィンドウは作らず、System Manager の各システム欄（このヘッダ配下）に直接出す。
    ImGui::Checkbox("Show Range Rings", &m_enabled);
    ImGui::Checkbox("Show Lock-on Line", &m_drawLine);
    ImGui::Checkbox("Show Current Range", &m_drawNow);
    ImGui::SliderFloat("Ring Height (m)", &m_ringHeight, 0.0f, 20.0f);

    ImGui::TextDisabled("半径は読み込み済みのBTから自動で拾う（手入力なし）");
    ImGui::TextDisabled("高さ0で地面の円に戻る");

    if (m_rings.empty() && m_edgeRings.empty())
    {
        ImGui::TextDisabled("距離条件が見つからない（BT未ロード / 距離判定なし）");
        return;
    }

    // 何mの線がどの向きの条件なのかを並べる。色だけでは戦闘中に読み切れない。
    for (const RangeRing &ring : m_rings)
    {
        const bool inside  = (ring.kinds & kInside)  != 0;
        const bool outside = (ring.kinds & kOutside) != 0;

        ImVec4 col = (inside && outside) ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f)
                   : inside              ? ImVec4(1.0f, 0.35f, 0.20f, 1.0f)
                                         : ImVec4(0.20f, 0.70f, 1.0f, 1.0f);

        const char *label = (inside && outside) ? "内側 / 外側（間合いの境目）"
                          : inside              ? "内側で成立 (<)"
                                                : "外側で成立 (>)";

        ImGui::TextColored(col, "%6.1f m  %s", ring.radius, label);
    }

    for (float radius : m_edgeRings)
        ImGui::TextColored(ImVec4(0.80f, 0.55f, 1.0f, 1.0f),
                           "%6.1f m  ステージ端（原点中心）", radius);
}


// NavMeshDebugDrawSystem と同じ R08_Main で ShapeRenderer に積む。
//
// 登録ごと切ることで「製品ビルドには存在しない」を1行で言い切る。
// 中身を #ifdef で刻むと、動かないコードが実行順の解決対象としてだけ残る。
#ifdef ENABLE_EDITOR
REGISTER_RENDER_SYSTEM(AIDebugSpatialSystem, Priority::RenderStage::R08_Main);
#endif
