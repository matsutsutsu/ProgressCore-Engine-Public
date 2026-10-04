#include "LockOnMarkerHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h" // 体の高さ

#include "Game/Logics/AI/Battle/BattleContext.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr DirectX::XMFLOAT4 kRed{0.95f, 0.18f, 0.22f, 1.0f}; // HUD の敵側の深紅
    constexpr float             kDefaultHeight = 4.0f;           // カプセルが無い敵の体の高さ（m）
    constexpr float             kRadToDeg      = 57.29578f;
} // namespace

bool LockOnMarkerHudSystem::ProjectTarget(EntityID target, DirectX::XMFLOAT2 &center) const
{
    const auto *camera = m_world->HasResource<Camera *>() ? m_world->GetResource<Camera *>() : nullptr;
    const auto *trans  = m_world->GetComponent<TransformComponent>(target);
    if (!camera || !trans) return false;

    // 体の中心の高さ。キャラクターのカプセルは足元が原点で、全体の高さは (halfHeight + radius) * 2
    float height = kDefaultHeight;
    if (const auto *capsule = m_world->GetComponent<JoltCapsuleColliderComponent>(target))
        height = (capsule->halfHeight + capsule->radius) * 2.0f;

    // 位置は描画補間済みの座標を使い、モデルの見た目に合わせる
    const Vector3 mid      = Vector3(trans->GetRenderWorldPosition()) + Vector3(0.0f, height * 0.5f, 0.0f);
    const Matrix  viewProj = camera->GetView() * camera->GetProjection();

    // Transform(coord) は w 除算込みなので、結果はそのまま NDC になる
    const Vector3 ndc = Vector3::Transform(mid, viewProj);
    if (ndc.z <= 0.0f || ndc.z >= 1.0f) return false;                  // カメラの後ろ・奥の切り捨て面より先
    if (std::fabs(ndc.x) > 1.0f || std::fabs(ndc.y) > 1.0f) return false; // 画面の外

    auto &gfx = Graphics::Instance();
    center    = {(ndc.x + 1.0f) * 0.5f * gfx.GetScreenWidth(), (1.0f - ndc.y) * 0.5f * gfx.GetScreenHeight()};
    return true;
}

void LockOnMarkerHudSystem::PushStroke(UIDrawList &list, float x, float y, float len, float thick, float angle,
                                       const DirectX::XMFLOAT4 &color) const
{
    // 暗い縁取り（一回り太く、半透明の黒）。明るい背景の上でも線の形が見えるように
    UIDrawItem back;
    back.kind      = UIDrawKind::Image;
    back.size      = {len + 2.0f, thick + 2.0f};
    back.pos       = {x - back.size.x * 0.5f, y - back.size.y * 0.5f}; // 矩形は中心で回るので、中心から左上へ戻す
    back.color     = {0.0f, 0.0f, 0.0f, 0.45f * color.w};
    back.rotation  = angle;
    back.sortOrder = kSortOrder - 1;
    list.items.push_back(back);

    UIDrawItem item = back;
    item.size      = {len, thick};
    item.pos       = {x - len * 0.5f, y - thick * 0.5f};
    item.color     = color;
    item.sortOrder = kSortOrder;
    list.items.push_back(item);
}

void LockOnMarkerHudSystem::Update(float dt)
{
    if (!m_world->HasResource<UIDrawList>()) return;
    auto &list = m_world->GetResource<UIDrawList>();

    // --- 今のロックオン対象 ---
    EntityID target = InvalidEntityID;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        if (const auto *player = battle.HasPlayer() ? m_world->GetComponent<PlayerComponent>(battle.playerEntity) : nullptr)
            target = player->lockOnTargetID;
    }
    if (target != InvalidEntityID && !m_world->IsEntityValid(target)) target = InvalidEntityID;

    // 対象が変わったら、締める演出を最初からやり直す
    if (target != InvalidEntityID && target != m_target) m_lockAge = 0.0f;
    if (target != InvalidEntityID) m_target = target;
    m_lockAge += dt;
    m_spin += kSpinSpeed * dt;

    // 投影できたときだけ位置を更新する。外れたら最後の位置に残したまま消していく
    DirectX::XMFLOAT2 center{};
    const bool        visible = (target != InvalidEntityID) && ProjectTarget(target, center);
    if (visible) m_center = center;

    const float goal = visible ? 1.0f : 0.0f;
    const float step = (goal > m_presence ? kFadeIn : kFadeOut) * dt;
    m_presence       = (goal > m_presence) ? std::min(goal, m_presence + step) : std::max(goal, m_presence - step);
    if (m_presence <= 0.001f) return;

    // --- 締まる演出: 開いた輪が減速しながら縮む。解除後は消えながら少し広がる ---
    const float acquire = std::clamp(m_lockAge / kAcquireTime, 0.0f, 1.0f);
    const float open    = (1.0f - acquire) * (1.0f - acquire); // 1 → 0（最後ほどゆっくり止まる）
    float       scale   = 1.0f + kAcquireOpen * open;
    if (!visible) scale += 0.5f * (1.0f - m_presence);
    const float R = kRadius * scale;

    // --- 色: 縮む間は白く光り、縮みきると深紅に落ち着く ---
    const float             white = 1.0f - std::clamp((m_lockAge - 0.05f) / 0.25f, 0.0f, 1.0f);
    const DirectX::XMFLOAT4 color{kRed.x + (1.0f - kRed.x) * white, kRed.y + (1.0f - kRed.y) * white,
                                  kRed.z + (1.0f - kRed.z) * white, m_presence};

    // --- 輪: 左右に切れ目のある2本の弧。円に沿って短い線を並べて作り、全体を m_spin だけ回す ---
    constexpr int   kArcs    = 2;
    constexpr float kArcSpan = 130.0f * 0.0174533f; // 1本の弧の広がり（ラジアン）。残りの 50 度ずつが切れ目
    constexpr int   kPerArc  = 10;                  // 1本の弧を作る線の数
    const float     segStep  = kArcSpan / kPerArc;
    const float     segLen   = R * segStep * 1.2f; // 隣と少し重ねて、つなぎ目の隙間を消す
    for (int arc = 0; arc < kArcs; ++arc)
    {
        const float start = m_spin + 3.1415927f * static_cast<float>(arc);
        for (int j = 0; j < kPerArc; ++j)
        {
            const float a = start + segStep * (static_cast<float>(j) + 0.5f);
            // 線の長い辺を円の接線にそろえる（横長の矩形を、角度 + 90 度回す）
            PushStroke(list, m_center.x + std::cos(a) * R, m_center.y + std::sin(a) * R, segLen, kThickness,
                       a * kRadToDeg + 90.0f, color);
        }
    }

    // --- 中心の◆ ---
    PushStroke(list, m_center.x, m_center.y, kCoreSize, kCoreSize, 45.0f, color);
}

// 他の収集 System と同じ帯で、描画リストへ積む（カメラは R01b_CameraBrain で決まっている）
REGISTER_RENDER_SYSTEM(LockOnMarkerHudSystem, Priority::RenderStage::R10b_UICollect);
