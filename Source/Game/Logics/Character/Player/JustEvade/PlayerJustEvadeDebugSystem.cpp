#include "PlayerJustEvadeDebugSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Graphics/SceneView/Camera.h" // 方向ボタンをカメラ基準の向きにする
#include "Game/Core/GameEvents.h"             // JustEvadeSuccessEvent

#include <imgui.h> // デバッグ UI(OnGui)

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kPendingTimeout = 1.0f; // ダッシュのクールダウン明けを待つ上限（秒）
    constexpr int   kForceSteps     = 3;    // 差し込んだダッシュが始まるのを待つステップ数
} // namespace

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void PlayerJustEvadeDebugSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 前のステップで始まったダッシュを、受付窓が開いているうちに成立させる
    // 2. 方向ボタンの要求があれば、その向きのダッシュを入力として差し込む
    // 3. 待機中なら、指定の遅れで攻撃入力を差し込む
    // 4. 終了の要求があれば、ラッシュを終わらせる

    ForEachWithID([&](EntityID                    id,
                      PlayerInputComponent       &pin,
                      CharacterMovementComponent &movement,
                      PlayerJustEvadeComponent         &evade,
                      PlayerDashComponent              &dash,
                      const PlayerComponent      &player,
                      PlayerRushComponent              &rush) {
        // ---------------------------------------------------------
        // 1. 成立させる
        // ---------------------------------------------------------
        TryForceSuccess(id, evade, dash, player);

        // 差し込んだのにダッシュが始まらなかった（硬直中など）ときは、成立の対象を取り消す。
        // 残しておくと、後で自分で押したダッシュが勝手に成立してしまう。
        if (m_forcedDir != EvadeDir::None)
        {
            --m_forceFramesLeft;
            if (m_forceFramesLeft <= 0)
            {
                m_lastResult = std::string("ダッシュが始まらず取り消し: ") + DirLabel(m_forcedDir);
                m_forcedDir  = EvadeDir::None;
            }
        }

        // ---------------------------------------------------------
        // 2. ダッシュを差し込む
        // ---------------------------------------------------------
        InjectDash(pin, movement, dash, rush, dt);

        // ---------------------------------------------------------
        // 3. 待機中の自動攻撃
        // ---------------------------------------------------------
        InjectAutoAttack(pin, rush, dt);

        // ---------------------------------------------------------
        // 4. ラッシュを終わらせる
        // ---------------------------------------------------------
        ForceEndRush(rush);
    });
}

// ---------------------------------------------------------------------------
// 差し込み
// ---------------------------------------------------------------------------

void PlayerJustEvadeDebugSystem::InjectDash(PlayerInputComponent &pin, CharacterMovementComponent &movement,
                                      const PlayerDashComponent &dash, const PlayerRushComponent &rush, float dt)
{
    if (m_pendingDir == EvadeDir::None)
    {
        return;
    }

    if (rush.isActive)
    {
        m_lastResult = "ラッシュ中のため取り消し";
        m_pendingDir = EvadeDir::None;
        return;
    }

    // ダッシュ中・クールダウン中は始められない。明けるまで少し待つ。
    if (dash.isDashing || dash.currentTimer > 0.0f)
    {
        m_pendingTimeout -= dt;
        if (m_pendingTimeout <= 0.0f)
        {
            m_lastResult = "クールダウンが明けず取り消し";
            m_pendingDir = EvadeDir::None;
        }
        return;
    }

    const Vector3 dir = ToWorldDir(m_pendingDir);
    if (dir.LengthSquared() < 1e-4f)
    {
        m_lastResult = "カメラが無いため取り消し";
        m_pendingDir = EvadeDir::None;
        return;
    }

    // 本物の入力と同じ形で書く。向きは PlayerDashSystem が desiredVelocity から決める。
    movement.desiredVelocity = dir;
    pin.dashPressed          = true;

    m_forcedDir       = m_pendingDir;
    m_forceFramesLeft = kForceSteps;
    m_pendingDir      = EvadeDir::None;
    m_lastResult      = std::string("ダッシュを差し込んだ: ") + DirLabel(m_forcedDir);
}

void PlayerJustEvadeDebugSystem::TryForceSuccess(EntityID id, PlayerJustEvadeComponent &evade, PlayerDashComponent &dash,
                                           const PlayerComponent &player)
{
    const bool wanted = m_alwaysSucceed || m_forcedDir != EvadeDir::None;
    if (!wanted)
    {
        return;
    }

    // PlayerJustEvadeSystem と同じ条件：受付窓が開いていて、このダッシュでまだ成立していない。
    if (!dash.isDashing || !dash.isJustEvadeWindowActive || evade.isJustSuccess)
    {
        return;
    }

    // 成立。書き方は PlayerJustEvadeSystem と揃える（受付を閉じ、成立フラグを立て、イベントを出す）。
    dash.isJustEvadeWindowActive = false;
    evade.isJustSuccess          = true;

    // 攻撃してきた敵の代わりにロックオン先を載せる（今は誰も読まない）。
    m_world->GetEventBus().Publish(JustEvadeSuccessEvent{id, player.lockOnTargetID});

    m_lastResult = std::string("成立: ") +
                   ((m_forcedDir != EvadeDir::None) ? DirLabel(m_forcedDir) : "手動のダッシュ");

    m_forcedDir       = EvadeDir::None;
    m_forceFramesLeft = 0;
}

void PlayerJustEvadeDebugSystem::InjectAutoAttack(PlayerInputComponent &pin, const PlayerRushComponent &rush, float dt)
{
    // 待機の外では数え直す（次の待機でもう一度入れられるように）。
    if (!rush.isActive || rush.phase != RushPhase::Ready)
    {
        m_readyElapsed     = 0.0f;
        m_autoAttackIssued = false;
        return;
    }

    if (!m_autoAttackReady || m_autoAttackIssued)
    {
        return;
    }

    m_readyElapsed += dt;
    if (m_readyElapsed >= m_autoAttackDelay)
    {
        // PlayerRushSystem が待機中の攻撃入力として受け取り、詰め→斬りへ進む。
        pin.attackPressed  = true;
        m_autoAttackIssued = true;
        m_lastResult += " → 自動攻撃";
    }
}

void PlayerJustEvadeDebugSystem::ForceEndRush(PlayerRushComponent &rush)
{
    if (!m_endRushRequested)
    {
        return;
    }
    m_endRushRequested = false;

    if (!rush.isActive)
    {
        m_lastResult = "ラッシュ中ではないため取り消し";
        return;
    }

    // 自由の時間切れにする。この後に走る PlayerRushSystem（L02）が同じステップで EndRush を呼ぶ。
    rush.phase   = RushPhase::Free;
    rush.timer   = 0.0f;
    m_lastResult = "ラッシュを終了";
}

// ---------------------------------------------------------------------------
// 向き
// ---------------------------------------------------------------------------

Vector3 PlayerJustEvadeDebugSystem::ToWorldDir(EvadeDir dir) const
{
    if (!m_world->HasResource<Camera *>())
    {
        return Vector3::Zero;
    }

    // PlayerInputSystem と同じく、カメラの前方と右方を水平にして使う。
    const Camera *camera  = m_world->GetResource<Camera *>();
    Vector3       forward = camera->GetFront();
    Vector3       right   = camera->GetRight();
    forward.y             = 0.0f;
    right.y               = 0.0f;
    forward.Normalize();
    right.Normalize();

    switch (dir)
    {
    case EvadeDir::Forward:
        return forward;
    case EvadeDir::Back:
        return -forward;
    case EvadeDir::Left:
        return -right;
    case EvadeDir::Right:
        return right;
    case EvadeDir::None:
        break;
    }
    return Vector3::Zero;
}

const char *PlayerJustEvadeDebugSystem::DirLabel(EvadeDir dir) const
{
    switch (dir)
    {
    case EvadeDir::Forward:
        return "前";
    case EvadeDir::Back:
        return "後";
    case EvadeDir::Left:
        return "左";
    case EvadeDir::Right:
        return "右";
    case EvadeDir::None:
        break;
    }
    return "-";
}

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void PlayerJustEvadeDebugSystem::OnGui()
{
    ImGui::TextWrapped("攻撃に触れずに、狙った向き（カメラ基準）でジャスト回避を成立させる。"
                       "ダッシュとラッシュは本番と同じ処理を通る。");

    // --- 方向ボタン（横に並べる） ---
    auto request = [this](EvadeDir dir) {
        m_pendingDir     = dir;
        m_pendingTimeout = kPendingTimeout;
    };

    const ImVec2 size(64.0f, 0.0f);

    if (ImGui::Button("前##JustEvadeFwd", size))
    {
        request(EvadeDir::Forward);
    }
    ImGui::SameLine();
    if (ImGui::Button("後##JustEvadeBack", size))
    {
        request(EvadeDir::Back);
    }
    ImGui::SameLine();
    if (ImGui::Button("左##JustEvadeLeft", size))
    {
        request(EvadeDir::Left);
    }
    ImGui::SameLine();
    if (ImGui::Button("右##JustEvadeRight", size))
    {
        request(EvadeDir::Right);
    }

    // --- ラッシュの終了 ---
    if (ImGui::Button("ラッシュを終わらせる"))
    {
        m_endRushRequested = true;
    }

    ImGui::Separator();

    // --- 常に成立・自動攻撃 ---
    ImGui::Checkbox("常に成立（WASD で好きな向きに試す）", &m_alwaysSucceed);
    ImGui::Checkbox("待機中に自動で攻撃", &m_autoAttackReady);
    if (m_autoAttackReady)
    {
        ImGui::DragFloat("攻撃までの遅れ", &m_autoAttackDelay, 0.01f, 0.0f, 3.0f, "%.2f 秒");
    }

    ImGui::Separator();
    ImGui::Text("直近: %s", m_lastResult.c_str());
    ImGui::TextDisabled("F2 でカーソルを出したままボタンを押しても差し込める");
}

// 入力の確定（PlayerInputSystem）の後、ダッシュの開始（PlayerDashSystem, L02）より前に入力を差し込む。
REGISTER_LOGIC_SYSTEM(PlayerJustEvadeDebugSystem, Priority::LogicStage::L01_Input);
