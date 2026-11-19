#include "Engine/Cinematic/Systems/CinematicCameraSystem.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/Cinematic/CinematicSubject.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Time/TimeService.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/Interpolation.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>
#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kSkipHoldSeconds   = 1.0f; // スキップに要る長押しの時間（秒）
    constexpr float kDofFadeOutSeconds = 1.0f; // 終わってからぼけを消すまでの時間（秒）
    constexpr int   kDofPriority       = 50;   // 被写界深度の上書きの優先度。ゲーム中の演出より手前
    const uint32_t  kDofSource         = CCL::Utils::HashString("Cinematic");

    // ショットが映す絵
    struct ShotPose
    {
        Vector3      eye;         // カメラ位置（ワールド、m）
        Vector3      look;        // 注視点（ワールド、m）
        float        fov = 45.0f; // 画角（度）
        CinematicDof dof;         // 被写界深度
    };



    // 再生位置 t で映すショットの番号。開始時刻が t 以下のうち最後のもの。列が空なら -1
    int FindShotIndex(const std::vector<CameraShot> &shots, float t)
    {
        if (shots.empty()) return -1;

        int index = 0;
        for (int i = 0; i < static_cast<int>(shots.size()); ++i)
        {
            if (shots[i].start <= t) index = i;
        }
        return index;
    }



    float ApplyEase(float t, CameraEase ease)
    {
        switch (ease)
        {
        case CameraEase::EaseIn:
            return t * t;
        case CameraEase::EaseOut:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case CameraEase::EaseInOut:
            return CCL::Math::SmoothStep(t);
        case CameraEase::Linear:
        default:
            return t;
        }
    }



    // ショットの頭から shotTime 秒の絵。subjectPos・yaw は基準の位置と向き（ラジアン、0 が +Z）
    ShotPose EvaluateShot(const CameraShot &shot, float shotTime, const Vector3 &subjectPos, float yaw)
    {
        const float progress = (shot.duration > 0.0f) ? std::clamp(shotTime / shot.duration, 0.0f, 1.0f) : 1.0f;
        const float e        = ApplyEase(progress, shot.ease);

        // ずれは基準の向きで回してから足す。対象がどちらを向いていても同じ構図になる
        const Matrix rot = Matrix::CreateRotationY(yaw);

        ShotPose pose;
        pose.look = subjectPos + Vector3::TransformNormal(Vector3::Lerp(shot.lookStart, shot.lookEnd, e), rot);

        if (shot.motion == CameraMotion::Orbit)
        {
            // 位置どうしを補間すると弦の上を通って対象に近づくので、角度・距離・高さを補間する
            const float   angle  = DirectX::XMConvertToRadians(std::lerp(shot.angleStart, shot.angleEnd, e));
            const float   radius = std::lerp(shot.radiusStart, shot.radiusEnd, e);
            const float   height = std::lerp(shot.heightStart, shot.heightEnd, e);
            const Vector3 offset(std::sin(angle) * radius, height, std::cos(angle) * radius);
            pose.eye = pose.look + Vector3::TransformNormal(offset, rot);
        }
        else
        {
            pose.eye = subjectPos + Vector3::TransformNormal(Vector3::Lerp(shot.eyeStart, shot.eyeEnd, e), rot);
        }

        pose.fov = std::lerp(shot.fovStart, shot.fovEnd, e);

        // ピントは注視点に合わせる。注視点が動けばピント送りになる
        pose.dof.focusDistance = Vector3::Distance(pose.eye, pose.look);
        pose.dof.focusRange    = shot.dofRange;
        pose.dof.falloff       = shot.dofFalloff;
        pose.dof.maxBlur       = shot.dof ? shot.dofMaxBlur : 0.0f;
        return pose;
    }



    void ApplyPose(VirtualCamera &vcam, const ShotPose &pose)
    {
        vcam.resultPos    = pose.eye;
        vcam.resultLookAt = pose.look;
        vcam.resultUp     = {0.0f, 1.0f, 0.0f};
        vcam.fov          = pose.fov;

        // 呼ばないと isValid が false のままで、Brain に選ばれない
        vcam.MarkResolved();
    }



    // 長押しが規定の時間に達したら true
    bool UpdateSkipHold(CinematicCameraComponent &cine, bool held, float dt)
    {
        if (!cine.script.allowSkip) return false;

        if (!held)
        {
            cine.skipArmed    = true;
            cine.skipHoldTime = 0.0f;
            return false;
        }

        // 再生の前から押しっぱなしの分は数えない。ゲームの操作で押していただけでスキップされないように
        if (!cine.skipArmed) return false;

        cine.skipHoldTime += dt;
        return cine.skipHoldTime >= kSkipHoldSeconds;
    }
}



void CinematicCameraSystem::Initialize()
{
    // 再生要求のたびにディスクを読まないよう、先にすべて読んでおく
    LoadAllScripts();

    ListenEvent<CinematicPlayRequestEvent>([this](const CinematicPlayRequestEvent &ev) {
        std::lock_guard<std::mutex> lock(m_requestMutex);

        // 同じフレームの2つ目以降は捨てる。どちらを流すかは頼む側で決める
        if (m_pending.play)
        {
            CCL_LOG_WARN(LogCategory::Game, "[Cinematic] 同じフレームに2つ目の再生要求が届いたので捨てました: %s",
                         ev.scriptPath.c_str());
            return;
        }
        m_pending.play      = true;
        m_pending.path      = ev.scriptPath;
        m_pending.startTime = ev.startTime;
        m_pending.script    = ev.script;
    });

    ListenEvent<CinematicStopRequestEvent>([this](const CinematicStopRequestEvent &) {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_pending.stop = true;
    });

    ListenEvent<CinematicScriptsChangedEvent>([this](const CinematicScriptsChangedEvent &) {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_pending.reload = true;
    });
}



void CinematicCameraSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いた要求を取り出す
    // 2. 停止要求と再生要求を受け付ける
    // 3. 再生位置を進める
    // 4. 再生中ならショットの絵を書き、終わりに達したら終える
    // 5. ゲームの操作を止めるかを書き、終了を知らせる

    // ---------------------------------------------------------
    // 1. 要求
    // ---------------------------------------------------------

    // 前のフレームから届いた停止・再生・読み直しの要求を、まとめて取り出す
    const Request request = TakeRequest();

    // 保存してすぐ再生されても新しい台本で流れるよう、再生要求より先に読み直す
    if (request.reload) LoadAllScripts();

    // ポーズ中は 0 になるので、ムービーも止まる
    const float playDt   = Time::PresentationDt(*m_world, dt);
    const bool  skipHeld = ReadSkipHeld();

    bool                                anyCamera  = false;
    bool                                anyPlaying = false;
    std::vector<CinematicFinishedEvent> finished;

    ForEach([&](VirtualCamera &vcam, CinematicCameraComponent &cine) {
        anyCamera = true;

        // ---------------------------------------------------------
        // 2. 停止と再生の受け付け
        // ---------------------------------------------------------

        // 止めた直後に同じフレームの再生要求を受けられるよう、停止を先に見る
        if (request.stop && cine.state == CinematicState::Playing)
        {
            cine.state   = CinematicState::Finished;
            cine.skipped = true;
            finished.push_back({cine.script.path, true});
        }

        // 再生要求の台本を写し、指定の位置から流し始める
        if (request.play) AcceptRequest(cine, request);

        // 流していなければ、プレビューを出すか、TPS カメラへ譲ってぼけを消す
        if (cine.state != CinematicState::Playing)
        {
            UpdateIdle(vcam, cine, dt);
            return;
        }

        // ---------------------------------------------------------
        // 3. 再生位置
        // ---------------------------------------------------------

        // 長押しでスキップが決まったら終わりへ飛ばし、決まらなければ進める
        if (UpdateSkipHold(cine, skipHeld, playDt))
        {
            cine.time    = cine.script.duration;
            cine.skipped = true;
        }
        else
        {
            cine.time = std::min(cine.time + playDt, cine.script.duration);
        }

        // ---------------------------------------------------------
        // 4. 絵と終了
        // ---------------------------------------------------------

        // 再生位置のショットの構図をカメラへ書く
        // 終わりの絵まで書いてから譲る。Brain がその絵から TPS カメラへ混ぜて戻す
        const bool wrote = WriteShotPose(vcam, cine);
        const bool ended = cine.time >= cine.script.duration;

        if (ended)
        {
            cine.state = CinematicState::Finished;
            finished.push_back({cine.script.path, cine.skipped});
        }

        const bool holding = wrote && !ended;
        vcam.priority      = holding ? cine.activePriority : cine.idlePriority;

        // ぼけは TPS カメラからこのカメラへ切り替わるブレンドに合わせて効かせる
        if (holding) UpdateDof(cine, UsesDof(cine.script) ? 1.0f : 0.0f, vcam.blendTime, dt);

        anyPlaying |= !ended;
    });

    // ---------------------------------------------------------
    // 5. 操作停止と終了の知らせ
    // ---------------------------------------------------------
    if (request.play && !anyCamera)
    {
        CCL_LOG_WARN(LogCategory::Game, "[Cinematic] ムービー用カメラがないので、再生要求を捨てました: %s",
                     request.path.c_str());
    }

    // 描画の段で書くので、ロジックの段（入力・敵の思考）には次の固定ステップから効く
    if (anyCamera && m_world->HasResource<GameFlowState>())
    {
        m_world->GetResource<GameFlowState>().cinematic = anyPlaying;
    }

    // 購読側はその場で呼ばれるので、Component の走査を抜けてから出す
    for (const auto &ev : finished)
    {
        CCL_LOG_INFO(LogCategory::Game, "[Cinematic] 再生終了: %s (skipped=%s)", ev.scriptPath.c_str(),
                     ev.skipped ? "yes" : "no");
        m_world->GetEventBus().Publish(ev);
    }
}



CinematicCameraSystem::Request CinematicCameraSystem::TakeRequest()
{
    std::lock_guard<std::mutex> lock(m_requestMutex);
    Request request = std::move(m_pending);
    m_pending       = Request{};
    return request;
}



void CinematicCameraSystem::AcceptRequest(CinematicCameraComponent &cine, const Request &request) const
{
    if (cine.state == CinematicState::Playing)
    {
        CCL_LOG_INFO(LogCategory::Game, "[Cinematic] 再生中のため、再生要求を捨てました: %s", request.path.c_str());
        return;
    }

    const CinematicScript *source = request.script.get();
    if (!source)
    {
        const auto found = m_scripts.find(request.path);
        if (found != m_scripts.end()) source = &found->second;
    }

    if (!source)
    {
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s は読み込まれていないので、再生要求を捨てました",
                      request.path.c_str());
        return;
    }

    // 写して持つ。再生中に台本を読み直しても、流しているムービーは変わらない
    cine.script = *source;

    // トラックはこの番号の変化で新しい再生に気付く。同じ台本を続けて流しても見分けられる
    ++cine.playCount;

    // 長さちょうどから始めると何も映らずに終わるので、長さの手前までにする
    const float startTime = std::clamp(request.startTime, 0.0f, std::max(0.0f, cine.script.duration - 0.001f));

    cine.state         = CinematicState::Playing;
    cine.time          = startTime;
    cine.playStartTime = startTime;
    cine.activeShot    = -1;
    cine.skipHoldTime  = 0.0f;
    cine.skipArmed     = false;
    cine.skipped       = false;

    CCL_LOG_INFO(LogCategory::Game, "[Cinematic] 再生開始: %s（%.2f 秒から）", request.path.c_str(), startTime);
}



void CinematicCameraSystem::UpdateIdle(VirtualCamera &vcam, CinematicCameraComponent &cine, float dt) const
{
    // エディタのプレビュー中は、指定の時刻の構図を書いてこのカメラを使わせる
    if (cine.previewActive && WritePreviewPose(vcam, cine))
    {
        vcam.priority = cine.activePriority;

        // つまみで時刻を飛ばして見るので、ぼけもすぐ切り替える
        UpdateDof(cine, UsesDof(cine.previewScript) ? 1.0f : 0.0f, 0.0f, dt);
        return;
    }

    // 姿勢は最後に書いた値のまま残す。Brain が TPS カメラへ戻すブレンドの補間元に使う
    vcam.priority = cine.idlePriority;
    UpdateDof(cine, 0.0f, kDofFadeOutSeconds, dt);
}



bool CinematicCameraSystem::WriteShotPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const
{
    const int index = FindShotIndex(cine.script.shots, cine.time);
    if (index < 0) return false;

    // ショットが替わっても同じカメラの姿勢を書き換えるだけなので、Brain のブレンドは入らずカットになる
    const CameraShot &shot = cine.script.shots[index];
    if (index != cine.activeShot)
    {
        EnterShot(cine, shot);
        cine.activeShot = index;
    }

    // 位置は毎フレーム取り直す。対象が動いても構図を保って付いていく
    Vector3 subjectPos = Vector3::Zero;
    if (!shot.subject.empty())
    {
        if (!m_world->IsEntityValid(cine.shotSubject)) return false;

        const auto *trans = m_world->GetComponent<TransformComponent>(cine.shotSubject);
        if (!trans) return false;

        subjectPos = trans->GetRenderWorldPosition();
    }

    const ShotPose pose = EvaluateShot(shot, cine.time - shot.start, subjectPos, cine.shotYaw);
    ApplyPose(vcam, pose);
    cine.dof = pose.dof;
    return true;
}



bool CinematicCameraSystem::WritePreviewPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const
{
    const std::vector<CameraShot> &shots = cine.previewScript.shots;

    const int index = FindShotIndex(shots, cine.previewTime);
    if (index < 0) return false;

    // 時刻が前後に飛ぶので、ショットに入った瞬間の基準を持てない。毎回名前で探す
    const CameraShot &shot = shots[index];
    Vector3           subjectPos;
    float             yaw = 0.0f;
    if (!Cinematic::FindSubjectPose(*m_world, shot.subject, subjectPos, yaw)) return false;

    const ShotPose pose = EvaluateShot(shot, cine.previewTime - shot.start, subjectPos, yaw);
    ApplyPose(vcam, pose);
    cine.dof = pose.dof;
    return true;
}



void CinematicCameraSystem::EnterShot(CinematicCameraComponent &cine, const CameraShot &shot) const
{
    cine.shotSubject = InvalidEntityID;
    cine.shotYaw     = 0.0f;
    if (shot.subject.empty()) return;

    // 名前で探すのは入った瞬間だけ。毎フレーム全 Entity の名前を比べない
    cine.shotSubject = Cinematic::FindEntityByName(*m_world, shot.subject);
    if (cine.shotSubject == InvalidEntityID)
    {
        CCL_LOG_WARN(LogCategory::Game, "[Cinematic] ショットの基準 '%s' が見つかりません", shot.subject.c_str());
        return;
    }

    // 向きを毎フレーム追うと、待機モーションの揺れや振り向きでカメラまで振り回される
    const auto *trans = m_world->GetComponent<TransformComponent>(cine.shotSubject);
    if (trans) cine.shotYaw = Cinematic::YawOf(*trans);
}



void CinematicCameraSystem::UpdateDof(CinematicCameraComponent &cine, float target, float fadeTime, float dt) const
{
    if (fadeTime <= 0.0f)
    {
        cine.dofWeight = target;
    }
    else
    {
        const float step = dt / fadeTime;
        cine.dofWeight   = (cine.dofWeight < target) ? std::min(target, cine.dofWeight + step)
                                                     : std::max(target, cine.dofWeight - step);
    }

    // シーンに DepthOfField がなければ、ぼかさない
    auto *dof = m_world->GetSingletonComponent<DepthOfFieldConfigComponent>();
    if (!dof) return;

    // 確保していなければ何もしないので、停止中に毎フレーム呼んでよい
    if (cine.dofWeight <= 0.0f)
    {
        dof->overrides.Release(kDofSource);
        return;
    }

    if (auto *ov = dof->overrides.Acquire(kDofSource, kDofPriority, dof->BaseParams()))
    {
        ov->weight               = cine.dofWeight;
        ov->params.focusDistance = cine.dof.focusDistance;
        ov->params.focusRange    = cine.dof.focusRange;
        ov->params.falloff       = cine.dof.falloff;
        ov->params.maxBlur       = cine.dof.maxBlur;
    }
}



bool CinematicCameraSystem::UsesDof(const CinematicScript &script)
{
    return std::any_of(script.shots.begin(), script.shots.end(), [](const CameraShot &s) { return s.dof; });
}



bool CinematicCameraSystem::ReadSkipHeld() const
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return false;

    const auto &inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    if (!inputAPI || !inputAPI->IsGameInputActive()) return false;

    // 長押しを数えるので、押している間ずっと true になる Jump を使う（Attack は押した瞬間だけ）
    return inputAPI->GetAction("Jump");
}



void CinematicCameraSystem::LoadAllScripts()
{
    m_scripts.clear();

    for (const std::string &path : FileSystem::ListFiles(kCinematicScriptDir, ".json"))
    {
        CinematicScript script;
        if (CinematicScriptIO::Load(path, script)) m_scripts.emplace(path, std::move(script));
    }

    CCL_LOG_INFO(LogCategory::Game, "[Cinematic] 台本を %zu 本読み込みました (%s)", m_scripts.size(),
                 kCinematicScriptDir);
}



// Brain（R01b）より前に姿勢を確定させる
REGISTER_RENDER_SYSTEM(CinematicCameraSystem, Priority::RenderStage::R01_Camera);
