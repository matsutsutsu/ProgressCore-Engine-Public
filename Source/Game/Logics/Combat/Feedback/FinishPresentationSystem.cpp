#include "Game/Logics/Combat/Feedback/FinishPresentationSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/Cinematic/CinematicScript.h"
#include "Engine/Cinematic/CinematicSubject.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Game/Core/GameEvents.h"
#include "Game/Logics/Combat/Feedback/FinishPresentationState.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace CCL::ECS;
using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr const char *kFinisherCinematic = "Assets/Cinematics/Finisher.json";   // スローの間のカメラ
    constexpr const char *kPhase2Cinematic   = "Assets/Cinematics/BossPhase2.json"; // 第2形態へ移るムービー
    constexpr const char *kDefeatCinematic   = "Assets/Cinematics/BossDefeat.json"; // 倒したときのムービー

    // フィニッシュのカメラの台本が終わる、これだけ手前でムービーへ移る（実時間の秒）
    // カメラは描画の時計で進むので、台本の長さちょうどだと先に終わり、TPS カメラが1フレーム映ることがある
    constexpr float kCutLead = 0.05f;

    constexpr float kMinHold      = 0.05f; // スローを保つ長さの下限（実時間の秒）
    constexpr float kFallbackHold = 0.85f; // フィニッシュのカメラの台本が読めないときの、スローを保つ長さ（実時間の秒）
}



void FinishPresentationSystem::Initialize()
{
    // とどめの一撃を出したときに受け取るイベント
    // 届くのは DamageResolutionSystem の処理の中なので、その場では始めず Update で始める
    ListenEvent<FinishBlowEvent>([this](const FinishBlowEvent &ev) { m_pendingTarget = ev.targetID; });

    if (!m_world->HasResource<FinishPresentationState>()) m_world->AddResource<FinishPresentationState>();
}



void FinishPresentationSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いたとどめから、演出を始める
    // 2. 演出の間、世界をスローに保つ
    // 3. スローが明けたら、フィニッシュのカメラからムービーへ移る

    // ---------------------------------------------------------
    // 1. 開始
    // ---------------------------------------------------------

    // 受けた側がボスなら、フィニッシュのカメラを流し、その長さだけスローを保つ
    if (m_pendingTarget != InvalidEntityID) Begin(m_pendingTarget);

    // メンバ変数に格納したのでInvalidでクリア
    m_pendingTarget = InvalidEntityID;

    // 演出中かを書く。リザルトは、これとムービーが両方終わるまで待つ
    // スローが明けたフレームのうちにムービーを頼むので、待ちが途切れない
    m_world->GetResource<FinishPresentationState>().active = m_holdTimer > 0.0f;

    // スロー時間が消えたら早期return
    if (m_holdTimer <= 0.0f) return;

    const auto *phase = m_world->GetComponent<BossPhaseComponent>(m_target);
    if (!phase)
    {
        m_holdTimer = 0.0f;
        return;
    }

    // ---------------------------------------------------------
    // 2. スロー
    // ---------------------------------------------------------

    // 毎フレーム頼み直して、決めた遅さのまま保つ
    // 頼むのをやめると、finishSlowRecover かけて等速へ戻る
    Time::SustainWorldSlow(*m_world, phase->finishSlowScale, phase->finishSlowRecover);

    // ---------------------------------------------------------
    // 3. ムービー
    // ---------------------------------------------------------

    // dt はスローのかからない実時間。スローの長さと同じ時計で数える
    m_holdTimer -= dt;
    if (m_holdTimer > 0.0f) return;

    // フィニッシュのカメラを止め、同じフレームにムービーを頼む
    // 再生中の要求は捨てられるので先に止める。間を空けると、TPS カメラへ戻る絵が挟まる
    m_world->GetEventBus().Publish(CinematicStopRequestEvent{});

    // ムービーが流れている間に、世界は等速へ戻っていく
    if (const char *movie = SelectMovie(*phase))
    {
        // ムービー演出を行うようにイベント送信
        m_world->GetEventBus().Publish(CinematicPlayRequestEvent{movie});
    }
}



void FinishPresentationSystem::Begin(EntityID targetID)
{
    // 移行演出中は無敵なので、普段は演出が重ならない
    if (m_holdTimer > 0.0f) return;

    const auto *phase = m_world->GetComponent<BossPhaseComponent>(targetID);
    if (!phase) return;

    m_target = targetID;

    // 今の画面から、プレイヤーの左前へ回り込むカメラを流す
    // 流れている間は、プレイヤーの操作とボスの思考が止まる
    m_holdTimer = PlayFinisherCamera();
}



float FinishPresentationSystem::PlayFinisherCamera()
{
    // 台本はとどめのたびに読み直す。ムービーエディタで直した構図を、すぐに試せる
    // 読めなければ（理由は Load がログに出す）、カメラは出さずにスローだけを決まった長さ保つ
    auto script = std::make_shared<CinematicScript>();
    if (!CinematicScriptIO::Load(kFinisherCinematic, *script)) return kFallbackHold;

    // 最初のショットだけを今の画面につなぐ。時刻と、2つ目以降のショットは台本のまま
    if (!script->shots.empty()) MatchShotStartToScreen(script->shots.front());

    // 中身を渡すと、CinematicCameraSystem は読み込み済みの台本ではなく、この写しを流す
    m_world->GetEventBus().Publish(CinematicPlayRequestEvent{kFinisherCinematic, 0.0f, script});

    return (std::max)(script->duration - kCutLead, kMinHold);
}



void FinishPresentationSystem::MatchShotStartToScreen(CameraShot &shot) const
{
    if (shot.motion != CameraMotion::Orbit) return;

    const Camera *camera = m_world->HasResource<Camera *>() ? m_world->GetResource<Camera *>() : nullptr;
    if (!camera) return;

    Vector3 subjectPos;
    float   yaw = 0.0f;
    if (!Cinematic::FindSubjectPose(*m_world, shot.subject, subjectPos, yaw)) return;

    const Vector3 eye     = camera->GetEye();
    Vector3       forward = Vector3(camera->GetFocus()) - eye;
    if (forward.LengthSquared() < 1e-6f) return;
    forward.Normalize();

    // ショットの向きは基準の向きで回してあるので、逆に回して基準から見た値にする
    const Matrix toWorld = Matrix::CreateRotationY(yaw);
    const Matrix toLocal = Matrix::CreateRotationY(-yaw);

    // 回り込みの中心は、画面の視線の上で、台本の注視点と同じ距離の所に置く
    // 画面のカメラの注視点はロックオン先など遠くのこともあり、そこを中心にすると大きく振れる
    const Vector3 scriptLook = subjectPos + Vector3::TransformNormal(shot.lookStart, toWorld);
    const Vector3 center     = eye + forward * Vector3::Distance(eye, scriptLook);

    // 中心から見たカメラのずれを、角度・距離・高さに直す
    const Vector3 offset = Vector3::TransformNormal(eye - center, toLocal);
    const float   angle  = DirectX::XMConvertToDegrees(std::atan2(offset.x, offset.z));

    shot.lookStart   = Vector3::TransformNormal(center - subjectPos, toLocal);
    shot.radiusStart = std::sqrt(offset.x * offset.x + offset.z * offset.z);
    shot.heightStart = offset.y;
    shot.fovStart    = DirectX::XMConvertToDegrees(camera->GetFovY());

    // 終わりの角度へ近い方から回す。遠い方だと、プレイヤーの反対側を大回りする
    shot.angleStart = shot.angleEnd + std::remainder(angle - shot.angleEnd, 360.0f);
}



const char *FinishPresentationSystem::SelectMovie(const BossPhaseComponent &phase) const
{
    if (phase.phase == BossPhase::Dead) return kDefeatCinematic;
    if (phase.phase == BossPhase::Transition) return kPhase2Cinematic;
    return nullptr;
}



// とどめが当たったフレームのうちにスローを頼むため、L06（ダメージの処理）の後に走らせる
REGISTER_LOGIC_SYSTEM(FinishPresentationSystem, Priority::LogicStage::L07_Cleanup);
