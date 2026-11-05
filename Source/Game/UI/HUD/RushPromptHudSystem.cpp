#include "Game/UI/HUD/RushPromptHudSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"

#include "Game/Logics/AI/Battle/BattleContext.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr uint32_t kAttackAction   = CCL::Utils::HashString("Attack"); // InputConfig.json の攻撃のアクション名
    constexpr float    kDefaultHeight  = 4.0f;      // カプセルがない敵の体の高さ（m）
    constexpr float    kScreenMargin   = 8.0f;      // 画面の端から空ける幅（px）
    constexpr float    kSegmentOverlap = 1.15f;     // リングの線を隣と重ねる倍率。つなぎ目の隙間を消す
    constexpr float    kPi             = 3.1415927f;
    constexpr float    kRadToDeg       = 57.29578f;
} // namespace



// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------

void RushPromptHudSystem::Update(float dt)
{
    // 処理フロー:
    // 1. プレイヤーのラッシュを読む
    // 2. 待機の出入りを見て、表示の状態を切り替える
    // 3. 濃さと残り時間を進める
    // 4. 位置・大きさ・濃さを決める
    // 5. リングとアイコンを積む

    if (!m_world->HasResource<UIDrawList>()) return;
    auto &list = m_world->GetResource<UIDrawList>();

    // ---------------------------------------------------------
    // 1. ラッシュの状態
    // ---------------------------------------------------------
    const EntityID playerID = FindPlayer();
    const auto    *rush     = m_world->GetComponent<PlayerRushComponent>(playerID);
    const auto    *player   = m_world->GetComponent<PlayerComponent>(playerID);

    // ---------------------------------------------------------
    // 2. 表示の状態
    // ---------------------------------------------------------
    SwitchState(rush);

    // 出ていない間は、調整値を探さずに抜ける
    // ラッシュしていない間の大半のフレームは、ここで終わる
    if (m_state == PromptState::Hidden) return;

    const RushPromptStyleComponent &style = GetStyle();

    // ---------------------------------------------------------
    // 3. 濃さと残り時間
    // ---------------------------------------------------------
    if (!AdvanceState(rush, style, dt)) return;

    // ---------------------------------------------------------
    // 4. 位置・大きさ・濃さ
    // ---------------------------------------------------------

    // 消えていく間は、最後の位置に残す
    if (m_state == PromptState::Showing) UpdateCenter(player, style);

    // 出た瞬間は、開いた大きさから減速しながら縮む
    const float appear = std::clamp(m_age / (std::max)(style.appearTime, 0.01f), 0.0f, 1.0f);
    float       scale  = 1.0f + style.appearOpen * (1.0f - appear) * (1.0f - appear);
    float       alpha  = m_presence;

    // 押せたときは、勢いよく広がりながら消える
    if (m_state == PromptState::Burst)
    {
        const float burst = std::clamp(m_stateTime / (std::max)(style.burstTime, 0.01f), 0.0f, 1.0f);
        scale += style.burstScale * (1.0f - (1.0f - burst) * (1.0f - burst));
        alpha *= 1.0f - burst;
    }

    // ---------------------------------------------------------
    // 5. リングとアイコン
    // ---------------------------------------------------------
    PushRing(list, style, scale, alpha);
    PushIcon(list, style, scale, alpha);
}



EntityID RushPromptHudSystem::FindPlayer() const
{
    if (!m_world->HasResource<BattleContext>()) return InvalidEntityID;

    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return InvalidEntityID;

    return battle.playerEntity;
}



void RushPromptHudSystem::SwitchState(const PlayerRushComponent *rush)
{
    const bool ready = rush && rush->isActive && rush->phase == RushPhase::Ready;

    // 待機に入ったら、最初から出し直す
    if (ready)
    {
        if (m_state != PromptState::Showing)
        {
            m_state     = PromptState::Showing;
            m_age       = 0.0f;
            m_stateTime = 0.0f;
            m_presence  = 0.0f;
            m_ratio     = 1.0f;
        }
        return;
    }

    if (m_state != PromptState::Showing) return;

    // 待機を抜けた瞬間。攻撃へ進んでいれば、時間内に押せている
    // 時間切れのときは、ラッシュごと終わって isActive が倒れている
    const bool pressed = rush && rush->isActive && rush->phase == RushPhase::Attack;
    if (pressed)
    {
        m_state = PromptState::Burst;
    }
    else
    {
        m_state = PromptState::FadeOut;
    }
    m_stateTime = 0.0f;
}



bool RushPromptHudSystem::AdvanceState(const PlayerRushComponent *rush, const RushPromptStyleComponent &style,
                                       float dt)
{
    m_age += dt;
    m_stateTime += dt;

    switch (m_state)
    {
    case PromptState::Showing:
        m_presence = (std::min)(1.0f, m_presence + style.fadeIn * dt);

        // 残り時間はラッシュの側で減っている。描くだけなので、割合をそのまま写す
        // Showing は待機中だけなので、rush は必ずある
        if (rush->readyDuration > 0.0f)
        {
            m_ratio = std::clamp(rush->timer / rush->readyDuration, 0.0f, 1.0f);
        }
        break;

    case PromptState::Burst:
        if (m_stateTime >= style.burstTime) m_state = PromptState::Hidden;
        break;

    case PromptState::FadeOut:
        m_presence = (std::max)(0.0f, m_presence - style.fadeOut * dt);
        if (m_presence <= 0.0f) m_state = PromptState::Hidden;
        break;

    default:
        break;
    }

    return m_state != PromptState::Hidden;
}



// ---------------------------------------------------------------------------
// 位置
// ---------------------------------------------------------------------------

void RushPromptHudSystem::UpdateCenter(const PlayerComponent *player, const RushPromptStyleComponent &style)
{
    auto       &gfx     = Graphics::Instance();
    const float screenW = static_cast<float>(gfx.GetScreenWidth());
    const float screenH = static_cast<float>(gfx.GetScreenHeight());

    EntityID target = InvalidEntityID;
    if (player) target = player->lockOnTargetID;
    if (!m_world->IsEntityValid(target)) target = InvalidEntityID;

    // --- 位置 ---
    if (target == InvalidEntityID)
    {
        m_center = {screenW * style.fallbackAnchor.x, screenH * style.fallbackAnchor.y};
    }
    else
    {
        // 照準と重ならないよう、敵の中心からずらす
        // 投影できない一瞬（カメラの切り替えなど）は、前の位置に残す
        XMFLOAT2 projected{};
        if (ProjectTarget(target, projected))
        {
            m_center = {projected.x + style.targetOffset.x, projected.y + style.targetOffset.y};
        }
    }

    // --- 画面の内側へ収める ---
    // 敵が画面の端にいると、ずらした分で合図がはみ出す
    const float margin = style.ringRadius + kScreenMargin;
    m_center.x         = std::clamp(m_center.x, margin, (std::max)(margin, screenW - margin));
    m_center.y         = std::clamp(m_center.y, margin, (std::max)(margin, screenH - margin));
}



bool RushPromptHudSystem::ProjectTarget(EntityID target, XMFLOAT2 &center) const
{
    if (!m_world->HasResource<Camera *>()) return false;

    const Camera *camera = m_world->GetResource<Camera *>();
    const auto   *trans  = m_world->GetComponent<TransformComponent>(target);
    if (!camera || !trans) return false;

    // カプセルは足元が原点で、全体の高さは (halfHeight + radius) * 2
    float height = kDefaultHeight;
    if (const auto *capsule = m_world->GetComponent<JoltCapsuleColliderComponent>(target))
    {
        height = (capsule->halfHeight + capsule->radius) * 2.0f;
    }

    // Vector3::Transform は w の除算まで行うので、結果はそのまま NDC になる
    const Vector3 mid      = Vector3(trans->GetRenderWorldPosition()) + Vector3(0.0f, height * 0.5f, 0.0f);
    const Matrix  viewProj = camera->GetView() * camera->GetProjection();
    const Vector3 ndc      = Vector3::Transform(mid, viewProj);

    // カメラの後ろ・奥の切り捨て面より先・画面の外は描かない
    if (ndc.z <= 0.0f || ndc.z >= 1.0f) return false;
    if (std::fabs(ndc.x) > 1.0f || std::fabs(ndc.y) > 1.0f) return false;

    auto &gfx = Graphics::Instance();
    center.x  = (ndc.x + 1.0f) * 0.5f * gfx.GetScreenWidth();
    center.y  = (1.0f - ndc.y) * 0.5f * gfx.GetScreenHeight();
    return true;
}



// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void RushPromptHudSystem::PushRing(UIDrawList &list, const RushPromptStyleComponent &style, float scale,
                                   float alpha) const
{
    // --- 色 ---

    // 残りわずかになったら、警告の色へ寄せる
    float warn = 0.0f;
    if (style.warnRatio > 0.0f && m_ratio < style.warnRatio)
    {
        warn = 1.0f - m_ratio / style.warnRatio;
    }

    XMFLOAT4 ringColor{};
    XMStoreFloat4(&ringColor, XMVectorLerp(XMLoadFloat4(&style.ringColor), XMLoadFloat4(&style.warnColor), warn));
    ringColor.w *= alpha;

    XMFLOAT4 trackColor = style.trackColor;
    trackColor.w *= alpha;

    // --- 線を並べる ---
    const float radius = style.ringRadius * scale;
    for (int i = 0; i < kRingSegments; ++i)
    {
        // 溝は一周ぶん敷き、減った分が見えるようにする
        // 明るい線の下に縁取りとして見えるよう、少し太くする
        PushRingSegment(list, radius, i, 1.0f, style.ringThickness + 2.0f, trackColor, kSortOrder - 1);

        // 残りの分だけ明るく描く。境目の線は、残りの分だけ短くする
        const float fill = std::clamp(m_ratio * kRingSegments - static_cast<float>(i), 0.0f, 1.0f);
        if (fill <= 0.0f) continue;

        PushRingSegment(list, radius, i, fill, style.ringThickness, ringColor, kSortOrder);
    }
}



void RushPromptHudSystem::PushRingSegment(UIDrawList &list, float radius, int index, float fill, float thickness,
                                          const XMFLOAT4 &color, int sortOrder) const
{
    const float step   = 2.0f * kPi / kRingSegments;
    const float length = radius * step * kSegmentOverlap * fill;

    // 線の中心の角度。真上（-90 度）から時計回りに進む
    // 画面は下が +Y なので、角度を増やすと時計回りになる
    const float angle = -0.5f * kPi + step * (static_cast<float>(index) + 0.5f * fill);
    const float x     = m_center.x + std::cos(angle) * radius;
    const float y     = m_center.y + std::sin(angle) * radius;

    // 横長の四角形を 90 度足して回し、長い辺を円の接線にそろえる
    // 四角形は中心で回るので、位置は中心から左上へ戻して渡す
    UIDrawItem item;
    item.kind      = UIDrawKind::Image;
    item.size      = {length, thickness};
    item.pos       = {x - length * 0.5f, y - thickness * 0.5f};
    item.rotation  = angle * kRadToDeg + 90.0f;
    item.color     = color;
    item.sortOrder = sortOrder;
    list.items.push_back(item);
}



void RushPromptHudSystem::PushIcon(UIDrawList &list, const RushPromptStyleComponent &style, float scale,
                                   float alpha) const
{
    // 画像が決まっていなければ、リングだけにする
    // パスが空のまま積むと、UIRenderer が白い四角を貼る
    const std::string &path = GetIconPath();
    if (path.empty()) return;

    const float size = style.iconSize * scale;

    UIDrawItem item;
    item.kind        = UIDrawKind::Image;
    item.texturePath = path;
    item.size        = {size, size};
    item.pos         = {m_center.x - size * 0.5f, m_center.y - size * 0.5f};
    item.color       = {1.0f, 1.0f, 1.0f, alpha};
    item.sortOrder   = kSortOrder + 1;
    list.items.push_back(item);
}



// ---------------------------------------------------------------------------
// 調整値と画像
// ---------------------------------------------------------------------------

const RushPromptStyleComponent &RushPromptHudSystem::GetStyle()
{
    static const RushPromptStyleComponent kDefaultStyle{};

    const auto *style = m_world->GetComponent<RushPromptStyleComponent>(m_styleID);
    if (style) return *style;

    // 見つけた子がない・消えていたら探し直す
    // PlayerHud はプレイヤーが現れてから作られるので、最初はないことがある
    m_styleID = InvalidEntityID;
    for (const EntityID id : m_world->View<RushPromptStyleComponent>())
    {
        style = m_world->GetComponent<RushPromptStyleComponent>(id);
        if (style)
        {
            m_styleID = id;
            break;
        }
    }
    if (!style) return kDefaultStyle;

    // 同じ子のアイコンを、攻撃の操作にする
    // アクション名のハッシュは保存されないので、見つけたときに書く
    if (auto *icon = m_world->GetComponent<InputIconComponent>(m_styleID))
    {
        icon->kind       = InputIconKind::Action;
        icon->actionHash = kAttackAction;
    }
    return *style;
}



const std::string &RushPromptHudSystem::GetIconPath() const
{
    static const std::string kEmpty;

    // 画像のパスは、InputIconSystem が今のデバイスに合わせて毎フレーム書いている
    // 子には UITransform がないので、UIImageCollectSystem はこの画像を描かない
    const auto *img = m_world->GetComponent<UIImageComponent>(m_styleID);
    if (!img) return kEmpty;

    return img->texturePath;
}



// 描画リストへ積む
REGISTER_RENDER_SYSTEM(RushPromptHudSystem, Priority::RenderStage::R10b_UICollect);
