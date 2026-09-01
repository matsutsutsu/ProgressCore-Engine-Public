// CameraIntroOrbitSystem の実装。

#include "Engine/Camera/Systems/CameraIntroOrbitSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Camera/CameraEvents.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"
#include "Engine/Core/App/GameFlowState.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // SmoothStep()

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
// スキップ入力を受け付けるまでの猶予(秒)。
// シーン開始直後にボタンを押しっぱなしだと事故で飛んでしまうため。
constexpr float kSkipGraceSeconds = 0.5f;

// 注視対象が見つかるまで待つ上限(秒)。
// これを超えたら演出を諦めてゲームを進行させる。
//
// 実測(Debug)で、レベルロード完了から対象の解決まで約1.1秒かかった。
// ロード直後の1フレーム目は dt が大きく跳ねるため、短すぎると遅い環境で
// 「ボスを見つける前に諦める」事故になる。待ちが長引いて困るのは
// 対象が本当に存在しない時だけなので、余裕を持たせるほうが安全側。
constexpr float kTargetResolveTimeout = 5.0f;
} // namespace

void CameraIntroOrbitSystem::Initialize()
{
    // 任意タイミングでの再生要求（形態移行でもう一度見せたい等）。
    ListenEvent<CameraIntroPlayRequestEvent>([this](const CameraIntroPlayRequestEvent &ev) {
        ForEachWithID([&](EntityID id, VirtualCamera &vcam, CameraBodyIntroOrbit &intro,
                          TransformComponent &trans) {
            if (ev.cameraID != InvalidEntityID && ev.cameraID != id) return;

            intro.playing      = true;
            intro.finished     = false;
            intro.skipped      = false;
            intro.previewing   = false;
            intro.elapsed      = 0.0f;
            intro.delayTimer   = 0.0f;
            intro.resolveTimer = 0.0f;
            vcam.priority      = intro.activePriority;
        });
    });
}

float CameraIntroOrbitSystem::ApplyEase(float t, CameraEaseMode mode)
{
    t = std::clamp(t, 0.0f, 1.0f);
    switch (mode)
    {
    case CameraEaseMode::EaseIn:    return t * t;
    case CameraEaseMode::EaseOut:   return 1.0f - (1.0f - t) * (1.0f - t);
    case CameraEaseMode::EaseInOut: return CCL::Math::SmoothStep(t);
    case CameraEaseMode::Linear:
    default:                        return t;
    }
}

bool CameraIntroOrbitSystem::AdvancePlayback(CameraBodyIntroOrbit &intro, float dt,
                                             bool skipRequested) const
{
    if (intro.finished) return false;

    // --- 自動再生の起動 ---
    if (!intro.playing && intro.autoPlayArmed && intro.playOnStart)
    {
        intro.delayTimer += dt;
        if (intro.delayTimer >= intro.startDelay)
        {
            intro.playing       = true;
            intro.autoPlayArmed = false;
        }
        // 待ちの間もカメラは開始位置に置いておきたいので、ここでは return しない。
        return false;
    }

    if (!intro.playing) return false;

    intro.elapsed += dt;

    // --- スキップ ---
    // 「即終了」ではなく「進行度を終端へ飛ばす」だけにしている。
    // こうすると次フレームで通常どおり finished になり、Brain の blendTime に
    // 乗って滑らかにプレイヤーカメラへ戻る（ハードカットにならない）。
    if (skipRequested && intro.allowSkip && intro.elapsed > kSkipGraceSeconds &&
        intro.elapsed < intro.duration)
    {
        intro.skipped = true;
        intro.elapsed = intro.duration;
    }

    if (intro.elapsed >= intro.duration)
    {
        intro.elapsed    = intro.duration;
        intro.playing    = false;
        intro.finished   = true;
        intro.previewing = false;
        return true;
    }
    return false;
}

void CameraIntroOrbitSystem::ApplyPose(VirtualCamera &vcam, TransformComponent &trans,
                                       const CameraBodyIntroOrbit &intro,
                                       const Vector3 &pivot, float eased) const
{
    // 角度・半径・高さを individually 補間する。
    // 最短回転にはしない：200→20 と書いたら「180度回り込む」という意図をそのまま出す。
    const float angleDeg = std::lerp(intro.startAngle, intro.endAngle, eased);
    const float radius   = std::lerp(intro.startRadius, intro.endRadius, eased);
    const float height   = std::lerp(intro.startHeight, intro.endHeight, eased);

    // TPS カメラと同じ球面座標の規約（X軸=cos, Z軸=sin）に合わせる。
    const float angleRad = XMConvertToRadians(angleDeg);

    Vector3 eye;
    eye.x = pivot.x + cosf(angleRad) * radius;
    eye.y = pivot.y + height;
    eye.z = pivot.z + sinf(angleRad) * radius;

    vcam.resultPos    = eye;
    vcam.resultLookAt = pivot; // 常に対象を見る＝対象が画面外へ出る事故が起きない
    vcam.resultUp     = Vector3::Up;
    vcam.fov          = std::lerp(intro.startFov, intro.endFov, eased);
    vcam.MarkResolved();

    // Transform にも焼いておく（ギズモ/デバッグ表示が正しい向きになるように）
    trans.position = eye;

    Vector3 forward = pivot - eye;
    if (forward.LengthSquared() > 1e-8f)
    {
        forward.Normalize();
        const XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), forward, Vector3::Up);
        XMVECTOR       det;
        trans.rotation = Quaternion::CreateFromRotationMatrix(XMMatrixInverse(&det, lookMat));
    }
}

void CameraIntroOrbitSystem::Abort(VirtualCamera &vcam, CameraBodyIntroOrbit &intro) const
{
    intro.playing       = false;
    intro.finished      = true;
    intro.autoPlayArmed = false;
    intro.previewing    = false;
    vcam.priority       = intro.idlePriority;
}

void CameraIntroOrbitSystem::Update(float dt)
{
    // スキップ入力。フレーム先頭で1回だけ読む。
    bool skipRequested = false;
    if (m_world->HasResource<std::shared_ptr<IInputAPI>>())
    {
        auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
        if (inputAPI && inputAPI->IsGameInputActive())
        {
            skipRequested = inputAPI->GetAction("Attack") || inputAPI->GetAction("Jump");
        }
    }

    // 終了通知は ForEach を抜けてから発行する。
    // イベントハンドラが同じチャンクを触りうるので、走査中に発行しない。
    struct FinishedEntry
    {
        EntityID id;
        bool     skipped;
    };
    std::vector<FinishedEntry> finished;

    // このフレーム、演出が進行中のカメラが1つでもあるか。
    bool anyCinematicActive = false;

    ForEachWithID([&](EntityID id, VirtualCamera &vcam, CameraBodyIntroOrbit &intro,
                      TransformComponent &trans) {
        // 既に終わっているカメラは待機優先度を維持するだけ。
        // 位置の再計算はしない（対象が動いてもどうせ選ばれないため）。
        if (intro.finished)
        {
            vcam.priority = intro.idlePriority;
            return;
        }

        // 出番待ちのカメラは、カメラも操作も一切掌握しない。
        //   playOnStart=false（イベント再生を待つ設定）のとき、ここを通さないと
        //   「再生が始まらないのに cinematic を握り続ける」＝操作不能のまま
        //   詰む。掌握してよいのは
        //     ・再生中
        //     ・自動再生の開始待ち（startDelay の消化中）
        //     ・インスペクタでスクラブ中
        //   の3つだけ。
        const bool waitingForAutoPlay = intro.autoPlayArmed && intro.playOnStart;
        if (!intro.playing && !waitingForAutoPlay && !intro.previewing)
        {
            vcam.priority = intro.idlePriority;
            return;
        }

        // 対象の解決。
        //   プレハブ生成は遅延されるため、シーン開始直後の数フレームは
        //   ボスがまだ存在しない。少し待ってから諦める。
        //   待っている間は vcam を MarkResolved しないので、CameraBrainSystem が
        //   この vCam を採用せず、勝手に原点へ飛ぶこともない。
        auto *targetTrans = m_world->GetComponent<TransformComponent>(intro.target.id);
        if (!targetTrans)
        {
            intro.resolveTimer += dt;
            if (intro.resolveTimer < kTargetResolveTimeout)
            {
                // まだ待つ。操作は止めたままにして、演出が始まる前に
                // プレイヤーが動き出さないようにする。
                anyCinematicActive = true;
                return;
            }

            //   ここで諦めないと、対象が現れないまま cinematic が立ち続け、
            //   プレイヤーが永久に操作できなくなる。
            //   演出の失敗は必ず「ゲームが進む」方向へ倒す。
            CCL_LOG_WARN(LogCategory::Game,
                "[CameraIntroOrbit] 対象エンティティが %.1f 秒見つからないため導入演出を中止します "
                "(camera=%llu, target=%llu)",
                kTargetResolveTimeout,
                static_cast<unsigned long long>(id),
                static_cast<unsigned long long>(intro.target.id));
            Abort(vcam, intro);
            finished.push_back({id, false});
            return;
        }
        intro.resolveTimer = 0.0f;

        const Vector3 pivot = Vector3(targetTrans->GetRenderWorldPosition()) + intro.targetOffset;

        const bool wasPlaying   = intro.playing;
        const bool justFinished = AdvancePlayback(intro, dt, skipRequested);

        // 演出のライフサイクルはログに残す。カメラが動かないときに
        // 「そもそも再生が始まっていないのか / 対象が違うのか」を切り分けられる。
        if (!wasPlaying && intro.playing)
        {
            CCL_LOG_INFO(LogCategory::Game,
                "[CameraIntroOrbit] 導入演出を開始 (camera=%s, target=%s, duration=%.2fs)",
                CCL::EntityLabel(*m_world, id).c_str(),
                CCL::EntityLabel(*m_world, intro.target.id).c_str(), intro.duration);
        }

        // 進行度。待ち時間中(playing=false かつ未終了)は 0 のまま＝開始位置で待機する。
        const float rawT  = (intro.duration > 0.0f)
                                ? std::clamp(intro.elapsed / intro.duration, 0.0f, 1.0f)
                                : 1.0f;
        const float eased = ApplyEase(rawT, intro.easeMode);

        ApplyPose(vcam, trans, intro, pivot, eased);

        if (justFinished)
        {
            CCL_LOG_INFO(LogCategory::Game, "[CameraIntroOrbit] 導入演出が終了 (camera=%s, skipped=%s)",
                CCL::EntityLabel(*m_world, id).c_str(), intro.skipped ? "yes" : "no");

            vcam.priority = intro.idlePriority; // ここで Brain が自動的に次のカメラへブレンドする
            finished.push_back({id, intro.skipped});
        }
        else
        {
            // 再生中・待機中はカメラを掌握する。
            vcam.priority      = intro.activePriority;
            anyCinematicActive = true;
        }
    });

    // カットシーン中フラグ。プレイヤー操作とボスBTを止めるための唯一の口。
    if (m_world->HasResource<GameFlowState>())
    {
        m_world->GetResource<GameFlowState>().cinematic = anyCinematicActive;
    }

    for (const auto &e : finished)
    {
        m_world->GetEventBus().Publish(CameraIntroFinishedEvent{e.id, e.skipped});
    }
}

// 他のカメラボディと同じステージ。Brain(R01b) より前で姿勢を確定させる。
REGISTER_RENDER_SYSTEM(CameraIntroOrbitSystem, Priority::RenderStage::R01_Camera);
