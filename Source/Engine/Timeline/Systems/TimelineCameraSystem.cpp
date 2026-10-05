// 役割・設計方針は TimelineCameraSystem.h を参照。
#include "Engine/Timeline/Systems/TimelineCameraSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/RenderInterpState.h" // 再生位置を描画フレームへ補間する alpha
#include "Engine/Core/Math/Interpolation.h"           // SmoothStep()

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    // 長さがこれ以下のベクトルは向きを論じられないものとして扱う
    constexpr float kMinLengthSq = 1e-8f;
} // namespace

// ===========================================================================
// 毎フレームの入口
// ===========================================================================

// ---------------------------------------------------------------------------
void TimelineCameraSystem::Update(float /*dt*/)
{
    // 再生位置はロジックの固定ステップで進むので、描画フレームへ補間してから評価する。
    // ポーズ中は Framework が alpha=1 にするので、最新の再生位置で止まる。
    float alpha = 1.0f;
    if (m_world->HasResource<RenderInterpState>())
        alpha = m_world->GetResource<RenderInterpState>().alpha;

    ForEach([&](VirtualCamera &vcam, TimelinePlayerComponent &player, TransformComponent &trans) {
        // カメラを掌握するのは、再生中・開始待ち・スクラブ中だけ。
        // 開始待ちで役割がまだ揃っていないときは、下の評価が失敗して譲る。
        const bool active = player.previewing || player.state == TimelinePlayState::Playing ||
                            player.state == TimelinePlayState::Waiting;

        if (!active || !player.asset || player.asset->cameraShots.empty())
        {
            vcam.priority      = player.idlePriority;
            player.activeShot  = -1;
            player.hasLastPose = false;
            return;
        }

        const float t = player.previewing ? player.time : std::lerp(player.prevTime, player.time, alpha);

        TimelineCameraPose pose;
        if (!EvaluateCamera(player, t, pose))
        {
            // 姿勢を出せないフレームは譲る。MarkResolved しないので、Brain が原点へ飛ぶこともない
            vcam.priority = player.idlePriority;
            return;
        }

        WritePose(vcam, trans, pose);
        vcam.priority = player.activePriority;
    });
}

// ===========================================================================
// 姿勢の計算（ショットの選択・固定・補間）
// ===========================================================================

// ---------------------------------------------------------------------------
bool TimelineCameraSystem::EvaluateCamera(TimelinePlayerComponent &player, float t, TimelineCameraPose &out) const
{
    const auto &shots = player.asset->cameraShots;

    // 1. 該当するショット（start が t 以下の最後のもの）を選ぶ。
    //    先頭より前・ショットの隙間は、直前のショットの最後の絵で待つ。
    int index = 0;
    for (int i = 0; i < static_cast<int>(shots.size()); ++i)
    {
        if (shots[i].start <= t) index = i;
    }
    const CameraShotClip &shot = shots[index];

    // 2. ショットが替わったら基準を固定し直し、Blend の補間元を決める
    if (index != player.activeShot)
    {
        if (!EnterShot(player, shot)) return false;

        // 最初のショットは補間しない（Brain が VirtualCamera.blendTime で入りを決める）
        player.hasBlend = shot.transitionIn == CameraShotTransition::Blend && player.activeShot >= 0 &&
                          player.hasLastPose && shot.blendTime > 0.0f;
        if (player.hasBlend) player.blendFrom = player.lastPose;

        player.activeShot = index;
    }

    // 3. ショット内の姿勢を出す
    const float local = std::clamp(t - shot.start, 0.0f, shot.duration);

    TimelineCameraPose pose;
    if (!EvaluateShot(player, shot, local, pose)) return false;

    // 4. Blend 指定なら、前のショットの最後の姿勢から補間する
    if (player.hasBlend && local < shot.blendTime)
    {
        const float w = CCL::Math::SmoothStep(local / shot.blendTime);
        pose.eye      = Vector3::Lerp(player.blendFrom.eye, pose.eye, w);
        pose.look     = Vector3::Lerp(player.blendFrom.look, pose.look, w);
        pose.fov      = std::lerp(player.blendFrom.fov, pose.fov, w);
    }

    player.lastPose    = pose;
    player.hasLastPose = true;
    out                = pose;
    return true;
}

// ---------------------------------------------------------------------------
bool TimelineCameraSystem::EnterShot(TimelinePlayerComponent &player, const CameraShotClip &shot) const
{
    const std::string &lookRole = shot.lookSubject.empty() ? shot.subject : shot.lookSubject;

    float lookYaw = 0.0f; // 注視の基準の向きは使わない（座標系は構図の基準に揃える）
    if (!ResolveRole(player, shot.subject, player.shotFrame.subjectPos, player.shotFrame.yaw)) return false;
    if (!ResolveRole(player, lookRole, player.shotFrame.lookPos, lookYaw)) return false;

    // 向きはショットの間ずっと固定する。毎フレーム追うと、待機モーションの揺れでカメラまで揺れる
    if (shot.space == CameraShotSpace::World) player.shotFrame.yaw = 0.0f;
    return true;
}

// ---------------------------------------------------------------------------
bool TimelineCameraSystem::EvaluateShot(const TimelinePlayerComponent &player, const CameraShotClip &shot,
                                        float local, TimelineCameraPose &out) const
{
    // 1. 基準の位置。followSubject なら今の位置、そうでなければ開始時に固定した位置
    Vector3 subjectPos = player.shotFrame.subjectPos;
    Vector3 lookPos    = player.shotFrame.lookPos;
    if (shot.followSubject)
    {
        float unusedYaw = 0.0f;
        if (!ResolveRole(player, shot.subject, subjectPos, unusedYaw)) return false;
        const std::string &lookRole = shot.lookSubject.empty() ? shot.subject : shot.lookSubject;
        if (!ResolveRole(player, lookRole, lookPos, unusedYaw)) return false;
    }

    // 2. 進行度と、オフセットを回す行列（SubjectLocal なら基準の向き、World なら回さない）
    const float   e   = TimelineAssetLoader::ApplyEase(local / shot.duration, shot.ease);
    const Matrix  rot = Matrix::CreateRotationY(player.shotFrame.yaw);

    // 3. 注視点（Truck / Orbit 共通）
    out.look = lookPos + Vector3::TransformNormal(Vector3::Lerp(shot.lookStart, shot.lookEnd, e), rot);

    // 4. カメラ位置
    if (shot.motion == CameraShotMotion::Orbit)
    {
        // 注視点を中心に回り込む。角度は TPS カメラと同じ規約（X軸=cos, Z軸=sin）。
        // 最短回転にはしない：90→-90 と書いたら「180度回り込む」という意図をそのまま出す。
        const float angle  = XMConvertToRadians(std::lerp(shot.angleStart, shot.angleEnd, e));
        const float radius = std::lerp(shot.radiusStart, shot.radiusEnd, e);
        const float height = std::lerp(shot.heightStart, shot.heightEnd, e);

        const Vector3 offset(std::cos(angle) * radius, height, std::sin(angle) * radius);
        out.eye = out.look + Vector3::TransformNormal(offset, rot);
    }
    else
    {
        out.eye = subjectPos + Vector3::TransformNormal(Vector3::Lerp(shot.eyeStart, shot.eyeEnd, e), rot);
    }

    out.fov = std::lerp(shot.fovStart, shot.fovEnd, e);
    return true;
}

// ===========================================================================
// 役割の解決と書き込み
// ===========================================================================

// ---------------------------------------------------------------------------
bool TimelineCameraSystem::ResolveRole(const TimelinePlayerComponent &player, const std::string &role,
                                       Vector3 &outPos, float &outYaw) const
{
    if (role.empty())
    {
        outPos = Vector3::Zero;
        outYaw = 0.0f;
        return true;
    }

    const auto it = player.bindings.find(role);
    if (it == player.bindings.end()) return false;
    if (!m_world->IsEntityValid(it->second)) return false;

    const auto *trans = m_world->GetComponent<TransformComponent>(it->second);
    if (!trans) return false;

    outPos = trans->GetRenderWorldPosition();

    // 描画行列の Z 軸（3行目）が前方。水平成分だけで向きを決める（見上げ角は使わない）
    const auto &m = trans->renderWorldMatrix;
    const float fx = m._31;
    const float fz = m._33;
    outYaw         = (fx * fx + fz * fz > kMinLengthSq) ? std::atan2(fx, fz) : 0.0f;
    return true;
}

// ---------------------------------------------------------------------------
void TimelineCameraSystem::WritePose(VirtualCamera &vcam, TransformComponent &trans,
                                     const TimelineCameraPose &pose) const
{
    vcam.resultPos    = pose.eye;
    vcam.resultLookAt = pose.look;
    vcam.resultUp     = Vector3::Up;
    vcam.fov          = pose.fov;
    vcam.MarkResolved();

    // Transform にも焼いておく（ギズモ/デバッグ表示が正しい向きになるように）
    trans.position = pose.eye;

    Vector3 forward = pose.look - pose.eye;
    if (forward.LengthSquared() > kMinLengthSq)
    {
        forward.Normalize();
        const XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), forward, Vector3::Up);
        XMVECTOR       det;
        trans.rotation = Quaternion::CreateFromRotationMatrix(XMMatrixInverse(&det, lookMat));
    }
}

// 他のカメラボディと同じステージ。Brain(R01b) より前で姿勢を確定させる。
REGISTER_RENDER_SYSTEM(TimelineCameraSystem, Priority::RenderStage::R01_Camera);
