#include "TitleCameraSystem.h"

#include "Engine/Core/Components/NameComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Game/Title/TitleComposition.h"

#include <cmath>
#include <cstring>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

DirectX::SimpleMath::Vector3 TitleCameraSystem::ResolveLookTarget()
{
    // 覚えていた目印が消えていたら忘れる（シーンを切り替えたときなど）
    if (m_lookTargetID != InvalidEntityID && !m_world->IsEntityValid(m_lookTargetID))
        m_lookTargetID = InvalidEntityID;

    // 見つかるまでは毎フレーム探す。プレハブの生成は後回しにされるので、最初の数フレームは
    // まだ無いことがある（Name と Transform を持つエンティティを1周するだけなので軽い）
    if (m_lookTargetID == InvalidEntityID)
    {
        m_world->ViewEach<NameComponent, TransformComponent>([&](EntityID id) {
            if (m_lookTargetID != InvalidEntityID) return;

            const auto *name = m_world->GetComponent<NameComponent>(id);
            if (name && std::strcmp(name->name, CCL::Title::kLookTargetName) == 0)
                m_lookTargetID = id;
        });
    }

    if (m_lookTargetID != InvalidEntityID)
    {
        if (const auto *trans = m_world->GetComponent<TransformComponent>(m_lookTargetID))
            return Vector3(trans->position);
    }

    return CCL::Title::DefaultLookTarget();
}

void TitleCameraSystem::PublishBoard(const DirectX::SimpleMath::Vector3 &eye,
                                     const DirectX::SimpleMath::Vector3 &lookTarget, const VirtualCamera &vcam)
{
    using CCL::Title::CompositionBoard;

    // ボードはカメラが構図を決めた後で初めて作る。先に空のボードを置くと、
    // 群れが原点を向いた構図で並んでしまう
    if (!m_world->HasResource<CompositionBoard>())
        m_world->AddResource<CompositionBoard>();

    CompositionBoard &board = m_world->GetResource<CompositionBoard>();

    auto       &gfx    = Graphics::Instance();
    const float height = gfx.GetScreenHeight();
    const float aspect = (height > 1.0f) ? (gfx.GetScreenWidth() / height) : (16.0f / 9.0f);

    const bool changed = (board.eye - eye).LengthSquared() > 1e-6f ||
                         (board.lookTarget - lookTarget).LengthSquared() > 1e-6f ||
                         std::fabs(board.fov - vcam.fov) > 1e-4f ||
                         std::fabs(board.aspect - aspect) > 1e-4f;

    board.eye        = eye;
    board.lookTarget = lookTarget;
    board.fov        = vcam.fov;
    board.aspect     = aspect;

    if (changed) ++board.revision;
}

DirectX::SimpleMath::Vector3 TitleCameraSystem::DriftedEye(
    const DirectX::SimpleMath::Vector3 &base, const DirectX::SimpleMath::Vector3 &pivot) const
{
    Vector3 offset = base - pivot;

    // 注視点を軸に水平に回すので、揺れても注視点は画面の同じ位置に残る
    if (m_swayPeriod > 0.01f)
    {
        const float phase = m_elapsed * (XM_2PI / m_swayPeriod);
        const float angle = std::sin(phase) * XMConvertToRadians(m_swayDegrees);
        const float c     = std::cos(angle);
        const float s     = std::sin(angle);

        offset = Vector3(offset.x * c + offset.z * s, offset.y, -offset.x * s + offset.z * c);
    }

    Vector3 eye = pivot + offset;

    // 上下の揺れ。横と周期をずらしてあるので、二つを合わせた動きはなかなか繰り返さない
    if (m_risePeriod > 0.01f)
        eye.y += std::sin(m_elapsed * (XM_2PI / m_risePeriod)) * m_riseAmplitude;

    return eye;
}

void TitleCameraSystem::ApplyPose(VirtualCamera &vcam, TransformComponent &trans,
                                  const DirectX::SimpleMath::Vector3 &eye, const DirectX::SimpleMath::Vector3 &pivot) const
{
    vcam.resultPos    = eye;
    vcam.resultLookAt = pivot;
    vcam.resultUp     = Vector3::Up;
    vcam.MarkResolved(); // 位置と注視点が決まったので、CameraBrainSystem がこの結果を使ってよい

    // 位置は書かない（レベルの値がそのまま構図の入力）。向きだけ注視点から求めて書く
    Vector3 forward = pivot - Vector3(trans.position);
    if (forward.LengthSquared() > 1e-8f)
    {
        forward.Normalize();
        const XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), forward, Vector3::Up);
        XMVECTOR       det;
        trans.rotation = Quaternion::CreateFromRotationMatrix(XMMatrixInverse(&det, lookMat));
        trans.isDirty  = true;
    }
}

void TitleCameraSystem::Update(float dt)
{
    if (!m_freezeDrift) m_elapsed += dt;

    const Vector3 pivot = ResolveLookTarget();

    ForEach([&](VirtualCamera &vcam, TransformComponent &trans) {
        const Vector3 base = Vector3(trans.position);

        PublishBoard(base, pivot, vcam);
        ApplyPose(vcam, trans, DriftedEye(base, pivot), pivot);
    });
}

void TitleCameraSystem::OnGui()
{
#ifdef ENABLE_EDITOR
    ImGui::TextUnformatted("タイトルカメラ（構図の調整）");

    if (m_world->HasResource<CCL::Title::CompositionBoard>())
    {
        const auto &board = m_world->GetResource<CCL::Title::CompositionBoard>();
        ImGui::Text("視点   (%.2f, %.2f, %.2f)", board.eye.x, board.eye.y, board.eye.z);
        ImGui::Text("注視点 (%.2f, %.2f, %.2f)%s", board.lookTarget.x, board.lookTarget.y,
                    board.lookTarget.z,
                    (m_lookTargetID != InvalidEntityID) ? "" : "  ※目印なし(既定値)");
        ImGui::Text("画角 %.1f度 / 構図の更新回数 %u", board.fov, board.revision);
    }

    ImGui::TextUnformatted("視点=Hierarchy の TitleCamera、注視点=TitleLookTarget をギズモで動かす");
    ImGui::TextUnformatted("決めたら File > Save Level で Title.level.json へ書き戻す");

    ImGui::Checkbox("揺れを止める（構図決め用）", &m_freezeDrift);

    ImGui::SliderFloat("横流れ[度]", &m_swayDegrees, 0.0f, 8.0f, "%.2f");
    ImGui::SliderFloat("横流れ周期[秒]", &m_swayPeriod, 4.0f, 60.0f, "%.1f");
    ImGui::SliderFloat("上下うねり[m]", &m_riseAmplitude, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("上下周期[秒]", &m_risePeriod, 4.0f, 60.0f, "%.1f");
#endif
}
