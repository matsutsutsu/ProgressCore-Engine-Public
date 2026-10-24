#include "AIDebugSpatialSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
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

    // 間合いの色（内側・外側・境目・ステージ端）は AIDebugRanges が持つ。ここはこの System だけが使う色。
    const XMFLOAT4 kColNow(1.0f, 0.35f, 1.0f, 1.0f);   // 今の間合い：マゼンタ
    const XMFLOAT4 kColLine(1.0f, 0.90f, 0.20f, 1.0f); // 照準線：黄

    // これより低ければ壁にせず地面の円で描く。
    constexpr float kFlatRingThreshold = 0.05f;

    // 塗りの分割。円周は DrawWireframeCircle と同じ 24 にして、線と面の輪郭を揃える。
    constexpr int kWallSegments = 24;

    // 縦の段数。色は面ごとにしか変えられないので、段を重ねて上へ行くほど薄くする。
    constexpr int kWallBands = 4;

    // 地面の帯は輪郭の線より少しだけ下へ置く。同じ高さだとちらつく。
    constexpr float kFillDrop = 0.02f;
} // namespace

// ---------------------------------------------------------------------------
// 図形の描画
// ---------------------------------------------------------------------------

void AIDebugSpatialSystem::DrawAnnulus(ShapeRenderer *renderer, const XMFLOAT3 &center,
                                       float rInner, float rOuter, const XMFLOAT4 &color) const
{
    for (int seg = 0; seg < kWallSegments; ++seg)
    {
        const float a0 = XM_2PI * seg / kWallSegments;
        const float a1 = XM_2PI * (seg + 1) / kWallSegments;

        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);

        const XMFLOAT3 outer0(center.x + c0 * rOuter, center.y, center.z + s0 * rOuter);
        const XMFLOAT3 outer1(center.x + c1 * rOuter, center.y, center.z + s1 * rOuter);

        // 内半径が 0 のときは扇形1枚で足りる（内側の2点が重なって面にならない）。
        if (rInner <= 0.01f)
        {
            renderer->DrawSolidTriangle({center.x, center.y, center.z}, outer0, outer1, color);
            continue;
        }

        const XMFLOAT3 inner0(center.x + c0 * rInner, center.y, center.z + s0 * rInner);
        const XMFLOAT3 inner1(center.x + c1 * rInner, center.y, center.z + s1 * rInner);

        renderer->DrawSolidTriangle(inner0, outer0, outer1, color);
        renderer->DrawSolidTriangle(inner0, outer1, inner1, color);
    }
}

void AIDebugSpatialSystem::DrawGroundBands(ShapeRenderer *renderer, const XMFLOAT3 &footCenter) const
{
    const XMFLOAT3 ground(footCenter.x, footCenter.y - kFillDrop, footCenter.z);

    float inner = 0.0f;
    for (size_t i = 0; i < m_rings.size(); ++i)
    {
        const RangeRing &ring = m_rings[i];

        // 帯の色は外側の境界線に合わせる。線と塗りで意味が食い違わないようにするため。
        const XMFLOAT4 &base = AIDebugRanges::ColorOf(ring);

        // 外側の帯ほど薄くする。手前ほど濃いので「近いほど危ない」がそのまま形に出る。
        const float t     = static_cast<float>(i) / static_cast<float>(m_rings.size());
        const float alpha = m_fillAlpha * (1.0f - t * 0.6f);

        DrawAnnulus(renderer, ground, inner, ring.radius, {base.x, base.y, base.z, alpha});
        inner = ring.radius;
    }
}

void AIDebugSpatialSystem::DrawRingWall(ShapeRenderer *renderer, const XMFLOAT3 &footCenter,
                                        float radius, const XMFLOAT4 &color) const
{
    const float bandHeight = m_ringHeight / kWallBands;

    for (int band = 0; band < kWallBands; ++band)
    {
        const float y0 = footCenter.y + bandHeight * band;
        const float y1 = y0 + bandHeight;

        // 足元の段が m_wallAlpha で、上の段ほど薄くする。
        const float    fade = 1.0f - static_cast<float>(band) / kWallBands;
        const XMFLOAT4 c(color.x, color.y, color.z, m_wallAlpha * fade);

        for (int seg = 0; seg < kWallSegments; ++seg)
        {
            const float a0 = XM_2PI * seg / kWallSegments;
            const float a1 = XM_2PI * (seg + 1) / kWallSegments;

            const float x0 = footCenter.x + std::cos(a0) * radius;
            const float z0 = footCenter.z + std::sin(a0) * radius;
            const float x1 = footCenter.x + std::cos(a1) * radius;
            const float z1 = footCenter.z + std::sin(a1) * radius;

            // 四角形1枚を三角形2枚で。裏面も描かれる設定なので巻き方向は問わない。
            renderer->DrawSolidTriangle({x0, y0, z0}, {x1, y0, z1}, {x0, y1, z0}, c);
            renderer->DrawSolidTriangle({x1, y0, z1}, {x1, y1, z1}, {x0, y1, z0}, c);
        }
    }
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
    //   線は輪郭として残す。塗りだけだと半径がどこなのかが曖昧になる。
    const XMFLOAT3 mid(footCenter.x, footCenter.y + m_ringHeight * 0.5f, footCenter.z);
    renderer->DrawWireframeCylinder(mid, radius, m_ringHeight, color);

    if (m_wallAlpha > 0.001f) DrawRingWall(renderer, footCenter, radius, color);
}

// ---------------------------------------------------------------------------
// System の入口
// ---------------------------------------------------------------------------

std::vector<CCL::ECS::TypeID> AIDebugSpatialSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(CCL::ECS::TypeInfo<BehaviorTreeComponent>::ID());
    return types;
}

void AIDebugSpatialSystem::Update(float /*dt*/)
{
    if (!m_enabled) return;

    // ShapeRenderer が無ければ描けない。
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    // プレイヤーの座標は BattleContext から取る。プレイヤーが居なければ照準線だけ描かない。
    XMFLOAT3 playerPos(0, 0, 0);
    bool     hasPlayer = false;

    // ステージ端のリングの中心と半径。条件と同じ円を描かないと、線と判定がずれる。
    XMFLOAT3 arenaCenter(0, 0, 0);
    float    arenaRadius = 0.0f;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        playerPos          = XMFLOAT3(battle.playerPos.x, battle.playerPos.y, battle.playerPos.z);
        hasPlayer          = battle.HasPlayer();
        arenaCenter        = battle.arenaCenter;
        arenaRadius        = battle.arenaRadius;
    }
    m_arenaRadius = arenaRadius;

    ForEachWithID([&](EntityID                  entity,
                      const TransformComponent &trans,
                      const BossActionComponent & /*boss*/) {
        // 地面から浮かせて Z ファイティングを避ける。地面に埋もれるときは GUI で上げる。
        const XMFLOAT3 center(trans.position.x, trans.position.y + m_heightOffset, trans.position.z);

        const auto *bt = m_world->GetComponent<BehaviorTreeComponent>(entity);
        if (bt && bt->sharedAsset)
            AIDebugRanges::CollectRings(*bt->sharedAsset, m_rings, m_edgeRings);
        else
        {
            m_rings.clear();
            m_edgeRings.clear();
        }

        // 塗りを先に置いてから線を重ねる。輪郭が帯に食われないようにするため。
        if (m_groundFill) DrawGroundBands(renderer, center);

        for (const RangeRing &ring : m_rings)
            DrawRangeRing(renderer, center, ring.radius, AIDebugRanges::ColorOf(ring));

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

        // ステージ端はステージの中心から。ボスの足元の高さに合わせておく。
        for (float margin : m_edgeRings)
        {
            const float radius = arenaRadius - margin;
            if (radius <= 0.0f) continue; // 幅がステージより広いと常に成立するので、描く輪が無い

            const XMFLOAT3 origin(arenaCenter.x, center.y, arenaCenter.z);
            DrawRangeRing(renderer, origin, radius, AIDebugRanges::kColEdge);
        }
    });
}

void AIDebugSpatialSystem::OnGui()
{
    // 別ウィンドウは作らず、System Manager の各システム欄（このヘッダ配下）に直接出す。
    ImGui::Checkbox("Show Range Rings", &m_enabled);
    ImGui::Checkbox("Show Lock-on Line", &m_drawLine);
    ImGui::Checkbox("Show Current Range", &m_drawNow);
    ImGui::Checkbox("Fill Ground Bands", &m_groundFill);
    ImGui::SliderFloat("Fill Alpha", &m_fillAlpha, 0.0f, 1.0f);
    ImGui::SliderFloat("Height Offset (m)", &m_heightOffset, 0.0f, 5.0f);

    ImGui::SliderFloat("Ring Height (m)", &m_ringHeight, 0.0f, 20.0f);
    if (m_ringHeight > 0.0f) ImGui::SliderFloat("Wall Alpha", &m_wallAlpha, 0.0f, 0.6f);

    ImGui::TextDisabled("半径は読み込み済みのBTから自動で拾う（手入力なし）");
    ImGui::TextDisabled("地面に埋もれるときは Height Offset を上げる");

    if (m_rings.empty() && m_edgeRings.empty())
    {
        ImGui::TextDisabled("距離条件が見つからない（BT未ロード / 距離判定なし）");
        return;
    }

    // 何mの線がどの向きの条件なのかを並べる。色だけでは戦闘中に読み切れない。
    for (const RangeRing &ring : m_rings)
    {
        const bool inside  = (ring.kinds & AIDebugRanges::kInside) != 0;
        const bool outside = (ring.kinds & AIDebugRanges::kOutside) != 0;

        const XMFLOAT4 &c   = AIDebugRanges::ColorOf(ring);
        const char     *label = (inside && outside) ? "内側 / 外側（間合いの境目）"
                                : inside            ? "内側で成立 (<)"
                                                    : "外側で成立 (>)";

        ImGui::TextColored(ImVec4(c.x, c.y, c.z, c.w), "%6.1f m  %s", ring.radius, label);
    }

    const XMFLOAT4 &edge = AIDebugRanges::kColEdge;
    for (float margin : m_edgeRings)
        ImGui::TextColored(ImVec4(edge.x, edge.y, edge.z, edge.w),
                           "%6.1f m  ステージ端から（中心から %.1f m）", margin, m_arenaRadius - margin);
}

// R08_Main で ShapeRenderer に積む
REGISTER_DEBUG_RENDER_SYSTEM(AIDebugSpatialSystem, Priority::RenderStage::R08_Main);
