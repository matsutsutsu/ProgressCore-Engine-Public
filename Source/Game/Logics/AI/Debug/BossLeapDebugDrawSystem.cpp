#include "BossLeapDebugDrawSystem.h"

#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    // 色。凡例は OnGui が同じ色で出す
    const XMFLOAT4 kTakeoffColor(0.3f, 0.6f, 1.0f, 1.0f);     // 踏み切り地点 … 青
    const XMFLOAT4 kLandingColor(1.0f, 0.6f, 0.15f, 1.0f);    // 予定の着地点と攻撃範囲 … 橙
    const XMFLOAT4 kPlannedColor(1.0f, 0.9f, 0.2f, 1.0f);     // 予定の軌道 … 黄
    const XMFLOAT4 kTrailColor(0.3f, 1.0f, 0.3f, 1.0f);       // 実際に通った軌道（着地の扱いになるまで）… 緑
    const XMFLOAT4 kAfterLandColor(1.0f, 0.25f, 0.25f, 1.0f); // 実際に通った軌道（着地の扱いになった後）… 赤

    // 地面(XZ平面)へ寝かせるための回転。円は XY 平面で定義されるので X軸+90°
    const XMFLOAT4 kGroundQuat(0.70710678f, 0.0f, 0.0f, 0.70710678f);

    constexpr float  kPointRadius        = 0.6f;  // 踏み切り地点・着地点の球の半径 [m]
    constexpr float  kHoldRadius         = 1.0f;  // 頂点で留まる位置の球の半径 [m]
    constexpr float  kLandedRadius       = 1.2f;  // 着地の扱いになった位置の球の半径 [m]
    constexpr int    kSegmentSamples     = 32;    // 予定の軌道1段を何本の線で描くか
    constexpr float  kTrailMinStep       = 0.05f; // 実際の軌道は、前の点からこれだけ動いたら点を足す [m]
    constexpr size_t kMaxTrailPoints     = 4000;  // 実際の軌道の点の上限。長い滞空でも際限なく増やさない
    constexpr float  kGroundGapTolerance = 0.3f;  // 着地の扱いになった瞬間の高さがこれ以内なら「地面で着地した」とみなす [m]

    // 跳躍の種類の表示名。添字は BossLeapKind の値
    const char *const kLeapKindNames[] = {"中央へ逃げる（放物線）", "跳躍攻撃（頂点でゆっくりになる放物線）", "滞空（真上で留まる）"};



    ImVec4 ToImVec4(const XMFLOAT4 &color) { return ImVec4(color.x, color.y, color.z, color.w); }



    // 2つの予約が同じ段か。経過時間は毎フレーム進むので比べない
    bool IsSameSegment(const CharacterMoveReservation &a, const CharacterMoveReservation &b)
    {
        return a.kind == b.kind && a.duration == b.duration && a.start == b.start && a.target == b.target;
    }
}



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void BossLeapDebugDrawSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 最初に見つかったボスの様子を記録する
    // 2. 記録を ShapeRenderer へ積む

    if (!m_enabled) return;

    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    // --- 記録 ---

    bool recorded = false;
    ForEach([&](const BossActionComponent &bossAction, const CharacterMovementComponent &movement,
                const JoltCharacterHandleComponent &joltHandle) {
        if (recorded || !joltHandle.character) return;
        recorded = true;

        Record(bossAction, movement, GetCharacterWorldPosition(joltHandle));
    });

    // --- 描画 ---

    if (m_record.valid) Draw(renderer);
}



// ---------------------------------------------------------------------------
// 記録
// ---------------------------------------------------------------------------

// このフレームのボスの様子を、最後の跳躍の記録に足す
void BossLeapDebugDrawSystem::Record(const BossActionComponent &bossAction, const CharacterMovementComponent &movement,
                                     const Vector3 &bossPos)
{
    // 処理フロー:
    // 1. 跳躍に入った瞬間なら、記録を作り直す
    // 2. 踏み切った瞬間に、踏み切り地点と予定の着地点を写す
    // 3. 予約した移動の段と、実際の位置を足す
    // 4. 着地の扱いになった瞬間の位置と時刻を写す

    const bool isLeaping = (bossAction.currentState == BossActionState::Leap);

    // --- 跳躍に入った瞬間 ---

    // 依頼番号も見るのは、跳躍を続けて出したときに前の記録と混ぜないため
    const bool isNewLeap = isLeaping && (!m_wasLeaping || bossAction.actionSerial != m_record.actionSerial);
    m_wasLeaping         = isLeaping;
    if (isNewLeap)
    {
        m_record              = LeapRecord{};
        m_record.valid        = true;
        m_record.actionSerial = bossAction.actionSerial;
        m_record.leapDef      = bossAction.leapDef;
    }

    // 跳躍の外では記録を足さない（最後の跳躍を描き続ける）
    if (!isLeaping) return;

    // --- 踏み切った瞬間 ---

    // 溜めの間は動かないので、踏み切ってから記録する
    if (bossAction.leapPhase == BossLeapPhase::Windup) return;
    if (!m_record.tookOff)
    {
        // 踏み切り地点・着地点は BossLeapAction::TakeOff が決めた値を写す
        m_record.tookOff     = true;
        m_record.takeoffPos  = bossAction.leapTakeoffPos;
        m_record.landingPos  = bossAction.leapLandingPos;
        m_record.takeoffTime = bossAction.actionTimer;
    }

    // --- 予約した移動の段 ---

    // 始点は物理が最初の更新で取るので、取った後（started）の予約を拾う
    // 着地の後は予約しないので、着地までの段だけが残る
    const CharacterMoveReservation &reservation = movement.reservation;
    const bool isPlannedMove = reservation.active && reservation.started && reservation.kind != MoveReservationKind::InDirection;
    if (isPlannedMove && (m_record.segments.empty() || !IsSameSegment(m_record.segments.back(), reservation)))
    {
        m_record.segments.push_back(reservation);
    }

    // --- 実際の位置 ---

    // 着地の扱いになった後も、跳躍が終わるまで足す（空中で着地の扱いになった後に落ちる様子も見える）
    const bool hasMoved = m_record.trail.empty() || Vector3::Distance(m_record.trail.back(), bossPos) >= kTrailMinStep;
    if (hasMoved && m_record.trail.size() < kMaxTrailPoints) m_record.trail.push_back(bossPos);

    // --- 着地の扱いになった瞬間 ---

    if (bossAction.leapPhase == BossLeapPhase::Recovery && !m_record.landed)
    {
        m_record.landed           = true;
        m_record.landedPos        = bossPos;
        m_record.landedTime       = bossAction.actionTimer;
        m_record.landedTrailIndex = m_record.trail.empty() ? 0 : m_record.trail.size() - 1;
    }
}



// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

// 最後の跳躍の記録を描く
void BossLeapDebugDrawSystem::Draw(ShapeRenderer *renderer) const
{
    if (!m_record.tookOff) return;

    // --- 踏み切り地点と予定の着地点 ---

    renderer->DrawSphere(m_record.takeoffPos, kPointRadius, kTakeoffColor);
    renderer->DrawSphere(m_record.landingPos, kPointRadius, kLandingColor);
    // 攻撃範囲の円。地面に埋もれないよう少し浮かせる
    Vector3 landingCircleCenter = m_record.landingPos;
    landingCircleCenter.y += 0.05f;
    renderer->DrawWireframeCircle(landingCircleCenter, m_landingRadius, kLandingColor, kGroundQuat);

    // --- 予定の軌道 ---

    for (const CharacterMoveReservation &segment : m_record.segments)
    {
        DrawPlannedSegment(renderer, segment);
    }

    // --- 実際に通った軌道 ---

    // 着地の扱いになった後は色を変える
    for (size_t i = 1; i < m_record.trail.size(); ++i)
    {
        const bool isAfterLanded = m_record.landed && i > m_record.landedTrailIndex;
        renderer->DrawLine(m_record.trail[i - 1], m_record.trail[i], isAfterLanded ? kAfterLandColor : kTrailColor);
    }

    // --- 着地の扱いになった瞬間 ---

    if (!m_record.landed) return;

    // 地面で着地していれば緑、空中なら赤。予定の着地点の高さまで縦線を引き、ずれを見せる
    const float     groundGap  = m_record.landedPos.y - m_record.landingPos.y;
    const bool      isOnGround = std::fabs(groundGap) <= kGroundGapTolerance;
    const XMFLOAT4 &color      = isOnGround ? kTrailColor : kAfterLandColor;
    renderer->DrawSphere(m_record.landedPos, kLandedRadius, color);

    Vector3 groundBelowLanded = m_record.landedPos;
    groundBelowLanded.y       = m_record.landingPos.y;
    renderer->DrawLine(m_record.landedPos, groundBelowLanded, color);
}



// 予約した移動の段を1つ描く
void BossLeapDebugDrawSystem::DrawPlannedSegment(ShapeRenderer *renderer, const CharacterMoveReservation &segment) const
{
    // 留まる段は、留まる位置に球を置く
    if (segment.kind == MoveReservationKind::Hold)
    {
        renderer->DrawSphere(segment.start, kHoldRadius, kPlannedColor);
        return;
    }

    // 運ぶ段は、物理と同じ式で点を打ってつなぐ
    Vector3 prevPos = ReservationPlannedPosition(segment, 0.0f);
    for (int i = 1; i <= kSegmentSamples; ++i)
    {
        const float   progress = static_cast<float>(i) / static_cast<float>(kSegmentSamples);
        const Vector3 nextPos  = ReservationPlannedPosition(segment, progress);
        renderer->DrawLine(prevPos, nextPos, kPlannedColor);
        prevPos = nextPos;
    }
}



// ---------------------------------------------------------------------------
// 数値
// ---------------------------------------------------------------------------

// 予定の軌道で一番高い所の、踏み切りからの高さ
float BossLeapDebugDrawSystem::PlannedApexHeight() const
{
    float apexY = m_record.takeoffPos.y;
    for (const CharacterMoveReservation &segment : m_record.segments)
    {
        for (int i = 0; i <= kSegmentSamples; ++i)
        {
            const float progress = static_cast<float>(i) / static_cast<float>(kSegmentSamples);
            apexY                = (std::max)(apexY, ReservationPlannedPosition(segment, progress).y);
        }
    }
    return apexY - m_record.takeoffPos.y;
}



// 実際に通った軌道で一番高い所の、踏み切りからの高さ
float BossLeapDebugDrawSystem::ActualApexHeight() const
{
    float apexY = m_record.takeoffPos.y;
    for (const Vector3 &trailPos : m_record.trail) apexY = (std::max)(apexY, trailPos.y);
    return apexY - m_record.takeoffPos.y;
}



// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void BossLeapDebugDrawSystem::OnGui()
{
    ImGui::Checkbox("跳躍の軌道を描く", &m_enabled);
    ImGui::SliderFloat("着地点の円の半径", &m_landingRadius, 0.0f, 30.0f, "%.1f m");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("跳躍攻撃の着地の判定（Leap_End_Attack）の半径に合わせる");

    if (!m_enabled)
    {
        ImGui::TextDisabled("描いている間だけ記録する");
        return;
    }

    // --- 色の凡例 ---

    ImGui::TextColored(ToImVec4(kTakeoffColor), "踏み切り");
    ImGui::SameLine();
    ImGui::TextColored(ToImVec4(kLandingColor), "予定の着地点・攻撃範囲");
    ImGui::SameLine();
    ImGui::TextColored(ToImVec4(kPlannedColor), "予定の軌道");
    ImGui::TextColored(ToImVec4(kTrailColor), "実際の軌道");
    ImGui::SameLine();
    ImGui::TextColored(ToImVec4(kAfterLandColor), "着地の扱いになった後");

    ImGui::Separator();

    // --- 最後の跳躍の数値 ---

    if (!m_record.valid || !m_record.tookOff)
    {
        ImGui::TextDisabled("まだ跳躍していない");
        return;
    }

    const size_t kindIndex = static_cast<size_t>(m_record.leapDef.kind);
    const char  *kindName  = (kindIndex < std::size(kLeapKindNames)) ? kLeapKindNames[kindIndex] : "?";
    ImGui::Text("跳躍の種類: %s", kindName);

    const float flatDistance = Vector3(m_record.landingPos.x - m_record.takeoffPos.x, 0.0f,
                                       m_record.landingPos.z - m_record.takeoffPos.z).Length();
    ImGui::Text("水平距離: %.1f m", flatDistance);
    ImGui::Text("頂点の高さ: 予定 %.1f m / 実際 %.1f m", PlannedApexHeight(), ActualApexHeight());

    // 予定の時間は、着地までに予約した段の長さの合計
    float plannedFlightTime = 0.0f;
    for (const CharacterMoveReservation &segment : m_record.segments) plannedFlightTime += segment.duration;

    if (!m_record.landed)
    {
        ImGui::Text("飛ぶ時間: 予定 %.2f 秒 / 実際 （飛んでいる）", plannedFlightTime);
        return;
    }

    const float actualFlightTime = m_record.landedTime - m_record.takeoffTime;
    ImGui::Text("飛ぶ時間: 予定 %.2f 秒 / 実際 %.2f 秒", plannedFlightTime, actualFlightTime);

    // 着地の扱いになった瞬間の、予定の着地点からの高さ。0 に近ければ地面で着地している
    const float groundGap  = m_record.landedPos.y - m_record.landingPos.y;
    const bool  isOnGround = std::fabs(groundGap) <= kGroundGapTolerance;
    ImGui::TextColored(ToImVec4(isOnGround ? kTrailColor : kAfterLandColor), "着地の扱いになった瞬間の高さ: %.2f m（%s）",
                       groundGap, isOnGround ? "地面で着地" : "空中で着地");

    const float landedMissDistance = Vector3(m_record.landedPos.x - m_record.landingPos.x, 0.0f,
                                             m_record.landedPos.z - m_record.landingPos.z).Length();
    ImGui::Text("予定の着地点からの水平のずれ: %.2f m", landedMissDistance);
}



// R08_Main で ShapeRenderer に積む
REGISTER_DEBUG_RENDER_SYSTEM(BossLeapDebugDrawSystem, Priority::RenderStage::R08_Main);
