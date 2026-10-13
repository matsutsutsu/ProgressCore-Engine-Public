#include "Engine/Cinematic/Systems/CinematicCameraSystem.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Time/TimeService.h" // PresentationDt（ポーズ中は 0）
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/Interpolation.h" // SmoothStep
#include "Engine/Core/App/GameFlowState.h"   // cinematic
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/FileSystem.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <imgui.h> // デバッグ UI(OnGui)
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace CCL::ECS;
using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kSkipHoldSeconds = 1.0f; // スキップに要る長押しの時間（秒）

    // ショットが映す絵
    struct ShotPose
    {
        Vector3 eye;         // カメラ位置（ワールド、m）
        Vector3 look;        // 注視点（ワールド、m）
        float   fov = 45.0f; // 画角（度）
    };

    // --- ショットの選択 ---

    /**
     * @brief 再生位置 t で映すショットの番号を返す。
     * @param shots 開始時刻の昇順に並んだショットの列。
     * @param t     再生位置（秒）。
     * @return 開始時刻が t 以下のショットのうち、最後のものの番号。先頭より前なら 0。列が空なら -1。
     */
    int FindShotIndex(const std::vector<CameraShot> &shots, float t)
    {
        if (shots.empty()) return -1;

        // 「t 以下の最後」にすると、ショットの隙間では前のショットが選ばれ続け（最後の絵で止まる）、
        // 重なりでは後から始まるショットが選ばれる（そこでカットされる）。

        // 覚えた番号から進めず、毎回先頭から探す。
        // 再生し直しなどで t が後ろへ戻っても、正しいショットが選ばれるようにするため。
        int index = 0;
        for (int i = 0; i < static_cast<int>(shots.size()); ++i)
        {
            if (shots[i].start <= t) index = i;
        }
        return index;
    }

    // --- ショットの絵 ---

    /**
     * @brief ショットの先頭からの経過時間を、進行度（0〜1）に直す。
     * @param time     ショットの先頭からの経過時間（秒）。
     * @param duration ショットの長さ（秒）。
     * @return 0〜1 の進行度。長さが 0 以下なら 1（最後の絵）。
     */
    float ShotProgress(float time, float duration)
    {
        // 長さ 0 で割ると無限大か NaN になり、clamp でも直せずカメラ位置が壊れる。
        // インスペクタでは 0 にできないが、JSON を手で書けば入るので、割る前に弾く。
        if (duration <= 0.0f) return 1.0f;

        return std::clamp(time / duration, 0.0f, 1.0f);
    }

    /**
     * @brief 進行度に速さのカーブを掛ける。
     * @param t    進行度（0〜1）。
     * @param ease 速さのカーブ。
     * @return カーブを掛けた進行度（0〜1）。
     */
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

    /**
     * @brief Orbit のカメラ位置を、注視点からのオフセットで返す。
     * @param shot Orbit のショット。
     * @param e    カーブを掛けた進行度（0〜1）。
     * @return 注視点からのオフセット（m、基準の向きで回す前）。
     */
    Vector3 OrbitOffset(const CameraShot &shot, float e)
    {
        // 位置ではなく角度・距離・高さを補間する。
        // 位置どうしを補間すると円ではなく弦の上を通り、途中で対象に近づいてしまう。

        // 角度は数値のまま補間する。開始と終了の差の分だけ、値が増えれば右回り、減れば左回りに回る。
        // 向きとして近い側へ回し直す計算はしないので、回る量と向きを書いた値で決められ、1周以上も回せる。
        const float angle  = DirectX::XMConvertToRadians(std::lerp(shot.angleStart, shot.angleEnd, e));
        const float radius = std::lerp(shot.radiusStart, shot.radiusEnd, e);
        const float height = std::lerp(shot.heightStart, shot.heightEnd, e);

        // 0 度が前（+Z）、90 度が右（+X）
        return {std::sin(angle) * radius, height, std::cos(angle) * radius};
    }

    /**
     * @brief ショットの、今の時刻での絵を求める。
     * @param shot       映すショット。
     * @param shotTime   ショットの先頭からの経過時間（秒）。
     * @param holdTime   ショットの頭で、動かさずに最初の絵で止めておく時間（秒）。
     * @param subjectPos 基準の位置（ワールド、m）。
     * @param yaw        基準の向き（ラジアン、0 が +Z）。
     * @return ワールドでのカメラ位置・注視点と、画角。
     */
    ShotPose EvaluateShot(const CameraShot &shot, float shotTime, float holdTime, const Vector3 &subjectPos,
                          float yaw)
    {
        // 処理フロー:
        // 1. ショット内の進行度に、速さのカーブを掛ける
        // 2. 注視点を求める
        // 3. カメラ位置を求める（Truck／Orbit）
        // 4. 画角を求める

        // 止めておく間は進行度 0 のままにし、終わってから残りの時間で最後まで動かす。
        // ショットの終わりの時刻は変えずに、書いた動きを頭から見せるため。
        // 止めておく時間がショットより長いと動く時間が無いので、最後の絵になる。
        const float progress = ShotProgress(shotTime - holdTime, shot.duration - holdTime);

        // ショット内の進行度にイージング関数をかけて補間する
        const float e = ApplyEase(progress, shot.ease);

        // オフセットは基準の向きで回してからワールドへ足す。
        // 対象がどちらを向いていても「対象から見て右後ろ」が同じ構図になる。
        const Matrix rot = Matrix::CreateRotationY(yaw);

        ShotPose      pose;
        const Vector3 lookOffset = Vector3::Lerp(shot.lookStart, shot.lookEnd, e);
        pose.look                = subjectPos + Vector3::TransformNormal(lookOffset, rot);

        // Truck は基準からのオフセット、Orbit は注視点からのオフセット
        if (shot.motion == CameraMotion::Orbit)
        {
            // 注視点からのオフセットを回してワールドへ足す
            pose.eye = pose.look + Vector3::TransformNormal(OrbitOffset(shot, e), rot);
        }
        else
        {
            // Truck は基準からのオフセットを回してワールドへ足す
            const Vector3 eyeOffset = Vector3::Lerp(shot.eyeStart, shot.eyeEnd, e);
            pose.eye                = subjectPos + Vector3::TransformNormal(eyeOffset, rot);
        }

        pose.fov = std::lerp(shot.fovStart, shot.fovEnd, e);
        return pose;
    }

    /**
     * @brief 入りのブレンド中なら、前のショットの絵から今のショットの絵へ混ぜる。
     * @param cine     補間元の絵を持つ再生状態。
     * @param shot     今のショット。
     * @param shotTime ショットの先頭からの経過時間（秒）。
     * @param pose     今のショットの絵。混ぜた絵で上書きする。
     */
    void BlendFromPrevious(const CinematicCameraComponent &cine, const CameraShot &shot, float shotTime,
                           ShotPose &pose)
    {
        // ブレンドの時間はショットの長さに含める。
        // 台本の開始時刻と絵が替わり始める時刻をそろえ、後に足す演出を時刻どおりに合わせられるようにするため。
        if (!cine.blendActive || shotTime >= shot.blendIn) return;

        // Brain のカメラ切り替えと同じ曲線にそろえる
        const float w = CCL::Math::SmoothStep(shotTime / shot.blendIn);
        pose.eye      = Vector3::Lerp(cine.blendFromPos, pose.eye, w);
        pose.look     = Vector3::Lerp(cine.blendFromLook, pose.look, w);
        pose.fov      = std::lerp(cine.blendFromFov, pose.fov, w);
    }

    // --- スキップ ---

    /**
     * @brief スキップボタンの長押しを数え、スキップするかを返す。
     * @param cine 長押しの状態を持つ再生状態。
     * @param held スキップボタンを今押しているか。
     * @param dt   経過時間（秒）。ポーズ中は 0。
     * @return 長押しが規定の時間に達したら true。
     */
    bool UpdateSkipHold(CinematicCameraComponent &cine, bool held, float dt)
    {
        if (!cine.script.allowSkip) return false;

        if (!held)
        {
            cine.skipArmed    = true;
            cine.skipHoldTime = 0.0f;
            return false;
        }

        // 押しっぱなしのまま再生に入った分は数えず、一度離してからの長押しだけを数える。
        // 再生の直前にゲームの操作でボタンを押していただけで、スキップされないようにするため。
        if (!cine.skipArmed) return false;

        cine.skipHoldTime += dt;
        return cine.skipHoldTime >= kSkipHoldSeconds;
    }
} // namespace

// ---------------------------------------------------------------------------
// 再生の進行
// ---------------------------------------------------------------------------

void CinematicCameraSystem::Initialize()
{
    // 再生要求のたびにファイルを読むと、その瞬間にディスクの読み込みが入るので、先にすべて読んでおく。
    LoadAllScripts();

    // 受け取りは mutex で守る。
    // 同じステージで並列に走る複数の System が、同時に発行してもよいように
    ListenEvent<CinematicPlayRequestEvent>([this](const CinematicPlayRequestEvent &ev) {
        std::lock_guard<std::mutex> lock(m_requestMutex);

        // 同じフレームに2つ目以降が届いたら、先に届いた方を流して後は捨てる。
        // 再生中に届いた要求を捨てるのと同じ考え方で、どちらを流すかの調整は頼む側の責務にする。
        if (m_hasRequest)
        {
            CCL_LOG_WARN(LogCategory::Game, "[Cinematic] 同じフレームに2つ目の再生要求が届いたので捨てました: %s",
                         ev.scriptPath.c_str());
            return;
        }
        m_pendingPath = ev.scriptPath;
        m_hasRequest  = true;
    });
}

void CinematicCameraSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いた再生要求と、スキップボタンの状態を読む
    // 2. 再生要求を受け付け、台本を写す（開始待ち・再生中・読み込んでいない台本なら捨てる）
    // 3. 開始待ちと再生位置を進める。長押しされたら終わりへ飛ぶ
    // 4. 開始待ち・再生中でなければ、優先度を下げて譲る
    // 5. 今の再生位置の絵をカメラへ書く
    // 6. 終わりに達したら終え、優先度を決める
    // 7. ゲームの操作を止めるかを書き、終了を知らせる

    // ---------------------------------------------------------
    // 1. 再生要求とスキップボタン
    // ---------------------------------------------------------

    // 取り出すと同時に空にする。
    bool        requested = false;
    std::string requestPath;
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        requested    = m_hasRequest;
        requestPath  = std::move(m_pendingPath);
        m_hasRequest = false;
        m_pendingPath.clear();
    }

    // 時間は描画フレームごとに実時間で進める。
    // ポーズ中は PresentationDt が 0 を返すので、ムービーも止まる。
    const float playDt = Time::PresentationDt(*m_world, dt);

    // スキップするかどうかの入力受付
    const bool skipHeld = ReadSkipHeld();

    bool anyCamera = false; // ムービー用カメラがあるか
    bool anyActive = false; // 開始待ち・再生中のムービーがあるか
    std::vector<CinematicFinishedEvent> finished;

    ForEach([&](VirtualCamera &vcam, CinematicCameraComponent &cine) {
        anyCamera = true;

        // ---------------------------------------------------------
        // 2. 再生要求の受け付け
        // ---------------------------------------------------------
        if (requested)
        {
            const auto found = m_scripts.find(requestPath);

            if (cine.state == CinematicState::Waiting || cine.state == CinematicState::Playing)
            {
                // 再生中のムービーを優先し、終わるまで次を受け付けない。開始待ちも再生中として扱う。
                CCL_LOG_INFO(LogCategory::Game, "[Cinematic] 再生中のため、再生要求を捨てました: %s",
                             requestPath.c_str());
            }
            else if (found == m_scripts.end())
            {
                // 読み込んでいない台本は流さない。
                // 流すと映すものが無いまま操作だけ止まるので、何も始めずにログで知らせる。
                CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s は読み込まれていないので、再生要求を捨てました",
                              requestPath.c_str());
            }
            else
            {
                // 台本は写して持つ。再生中に台本を読み直しても、流しているムービーは変わらない。
                cine.script = found->second;

                cine.state        = (cine.script.startDelay > 0.0f) ? CinematicState::Waiting : CinematicState::Playing;
                cine.time         = 0.0f;
                cine.waitTime     = 0.0f;
                cine.activeShot   = -1;
                cine.skipHoldTime = 0.0f;
                cine.skipArmed    = false;
                cine.skipped      = false;
            }
        }

        // ---------------------------------------------------------
        // 3. 開始待ちと再生位置
        // ---------------------------------------------------------
        if (cine.state == CinematicState::Waiting || cine.state == CinematicState::Playing)
        {
            // スキップの長押しをカウント
            if (UpdateSkipHold(cine, skipHeld, playDt))
            {
                // 開始待ちの途中でも、終わりの位置へ飛ぶ。
                // 終わりの絵を 5 で書いてから 6 で終えるので、最後の絵を経て TPS カメラへ戻る。
                cine.state   = CinematicState::Playing;
                cine.time    = cine.script.duration;
                cine.skipped = true;
            }
            // 開始待ち秒数のカウント
            else if (cine.state == CinematicState::Waiting)
            {
                // 待っている間は再生位置を 0 のままにして、最初の絵で止めておく。
                cine.waitTime += playDt;
                if (cine.waitTime >= cine.script.startDelay) cine.state = CinematicState::Playing;
            }
            else
            {
                // timeを更新
                cine.time = std::min(cine.time + playDt, cine.script.duration);
            }
        }

        // ---------------------------------------------------------
        // 4. 停止中
        // ---------------------------------------------------------
        if (cine.state != CinematicState::Waiting && cine.state != CinematicState::Playing)
        {
            // 停止中は優先度を下げるだけで、姿勢は最後に書いた値のまま残す。
            // Brain は TPS カメラへ戻すブレンドの補間元に、この最後の姿勢を使う。
            vcam.priority = cine.idlePriority;
            return;
        }

        // ---------------------------------------------------------
        // 5. 絵の書き込み
        // ---------------------------------------------------------

        // ショットのブレンドや描画などの処理
        const bool wrote = WriteShotPose(vcam, cine);

        // ---------------------------------------------------------
        // 6. 終了と優先度
        // ---------------------------------------------------------

        // 再生中で、再生位置が長さを超えたら終了する。
        const bool reachedEnd = (cine.state == CinematicState::Playing) && (cine.time >= cine.script.duration);

        if (reachedEnd)
        {
            // 再生位置が長さを超えたら終了
            // 終わりの絵は 5 で書いてある。このフレームから譲り、Brain がその絵から TPS カメラへ混ぜて戻す。
            cine.state = CinematicState::Finished;
            finished.push_back({cine.script.path, cine.skipped});
        }

        // 再生中は優先度を上げ、停止中は下げる
        vcam.priority = (wrote && !reachedEnd) ? cine.activePriority : cine.idlePriority;

        // 開始待ち・再生中なら、ゲームの操作を止める
        if (cine.state != CinematicState::Finished) anyActive = true;
    });

    // ---------------------------------------------------------
    // 7. 操作停止と終了の知らせ
    // ---------------------------------------------------------

    // ムービー用カメラが無いシーンでは触らない。
    // 描画の段で書くので、ロジックの段（プレイヤーの入力・敵の思考）には次の固定ステップから効く。
    // 同じ段の CameraActionTPSSystem もこれを読むが、どちらも VirtualCamera を書くので同時には走らない。
    if (anyCamera && m_world->HasResource<GameFlowState>())
        m_world->GetResource<GameFlowState>().cinematic = anyActive;

    // 終了の知らせは、ForEach を抜けてから出す。
    // 購読側はその場で呼ばれるので、購読側の処理を Component の走査と重ねないため。
    for (const auto &ev : finished)
    {
        CCL_LOG_INFO(LogCategory::Game, "[Cinematic] 再生終了: %s (skipped=%s)", ev.scriptPath.c_str(),
                     ev.skipped ? "yes" : "no");
        m_world->GetEventBus().Publish(ev);
    }
}

bool CinematicCameraSystem::ReadSkipHeld() const
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return false;

    const auto &inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    if (!inputAPI || !inputAPI->IsGameInputActive()) return false;

    // 長押しを数えるので、押している間ずっと true になる Jump（Down）を使う。
    // Attack は押した瞬間だけ true（Triggered）なので、長押しを数えられない。
    return inputAPI->GetAction("Jump");
}

// ---------------------------------------------------------------------------
// 台本の読み込み
// ---------------------------------------------------------------------------

void CinematicCameraSystem::LoadAllScripts()
{
    m_scripts.clear();

    // 指定したフォルダの中にある .json ファイルをすべて読み込む。
    for (const std::string &path : FileSystem::ListFiles(kCinematicScriptDir, ".json"))
    {
        CinematicScript script;
        if (CinematicScriptIO::Load(path, script)) m_scripts.emplace(path, std::move(script));
    }

    CCL_LOG_INFO(LogCategory::Game, "[Cinematic] 台本を %zu 本読み込みました (%s)", m_scripts.size(),
                 kCinematicScriptDir);
}

// ---------------------------------------------------------------------------
// カメラへの書き込み
// ---------------------------------------------------------------------------

bool CinematicCameraSystem::WriteShotPose(VirtualCamera &vcam, CinematicCameraComponent &cine) const
{
    // 処理フロー:
    // 1. 再生位置に合うショットを選び、替わった瞬間なら入る準備をする
    // 2. 基準の位置を取る
    // 3. ショットの絵を求め、入りのブレンド中なら前の絵から混ぜる
    // 4. カメラへ書く

    // ---------------------------------------------------------
    // 1. ショットの選択
    // ---------------------------------------------------------

    // 再生中は、再生位置に合うショットを探す。
    const int index = FindShotIndex(cine.script.shots, cine.time);

    // ショットが1つも無ければ映すものが無い
    if (index < 0) return false;

    // ショットが替わっても、同じカメラの姿勢を書き換えるだけにする。
    // Brain から見ると同じカメラが選ばれ続けるので、切り替えのブレンドが入らずカットになる。
    const CameraShot &shot = cine.script.shots[index];
    if (index != cine.activeShot)
    {
        // 入りのブレンドの補間元は、替わる直前にこのカメラへ書いた絵（画面に出ていた絵）を止めて持つ。
        // 前のショットを計算し続ける形だと、入りのブレンド中にさらに替わったとき、
        // 補間元が「混ぜている途中の絵」から「前のショットだけの絵」へ変わり、画面が跳ぶ。

        // 最初のショットは混ぜない。
        // TPS カメラからの切り替えは、Brain がこのカメラの blendTime で混ぜる。
        cine.blendActive = (cine.activeShot >= 0) && (shot.blendIn > 0.0f);
        if (cine.blendActive)
        {
            cine.blendFromPos  = vcam.resultPos;
            cine.blendFromLook = vcam.resultLookAt;
            cine.blendFromFov  = vcam.fov;
        }

        // ショットが変わった瞬間に、基準の対象と向きを決める
        EnterShot(cine, shot);
        cine.activeShot = index;
    }

    // ---------------------------------------------------------
    // 2. 基準の位置
    // ---------------------------------------------------------

    // 基準の位置は毎フレーム取り直す。対象が動いても、構図を保ったまま付いていく。
    Vector3 subjectPos;

    // 対象のEntityの座標を取得する
    // 基準が無いと姿勢を決められない
    if (!ResolveSubjectPosition(cine, shot, subjectPos)) return false;

    // ---------------------------------------------------------
    // 3. ショットの絵
    // ---------------------------------------------------------
    const float shotTime = cine.time - shot.start;

    // 入りのブレンド中は、ショットの動きを最初の絵で止めておく
    const float holdTime = cine.blendActive ? shot.blendIn : 0.0f;

    // ショットの今の絵を求める
    ShotPose pose = EvaluateShot(shot, shotTime, holdTime, subjectPos, cine.shotYaw);

    // ショット同士の切り替えブレンド確認
    BlendFromPrevious(cine, shot, shotTime, pose);

    // ---------------------------------------------------------
    // 4. カメラへの書き込み
    // ---------------------------------------------------------
    vcam.resultPos    = pose.eye;
    vcam.resultLookAt = pose.look;
    vcam.resultUp     = {0.0f, 1.0f, 0.0f};

    // 停止中は書かない。TPS カメラへ戻すブレンドの補間元に、最後の画角を残すため。
    vcam.fov = pose.fov;

    // 位置と注視点の両方をここで確定させたので、Brain が採用してよい状態にする。
    // これを呼ばないと isValid が false のままで、優先度を上げても選ばれない。
    vcam.MarkResolved();
    return true;
}

// ---------------------------------------------------------------------------
// ショットの基準
// ---------------------------------------------------------------------------

void CinematicCameraSystem::EnterShot(CinematicCameraComponent &cine, const CameraShot &shot) const
{
    cine.shotSubject = InvalidEntityID;
    cine.shotYaw     = 0.0f;

    if (shot.subject.empty()) return;

    // 名前で探すのは、ショットに入った瞬間の1回だけ。
    // 毎フレームは見つけた Entity から直接引く（名前の比較を全 Entity に対して繰り返さない）。
    cine.shotSubject = FindEntityByName(shot.subject);

    // 見つからなければ、ショットの基準は無いものとして扱う
    if (cine.shotSubject == InvalidEntityID)
    {
        CCL_LOG_WARN(LogCategory::Game, "[Cinematic] ショットの基準 '%s' が見つかりません", shot.subject.c_str());
        return;
    }

    const auto *trans = m_world->GetComponent<TransformComponent>(cine.shotSubject);
    if (!trans) return;

    // 向きは、ショットに入った瞬間の値でショットの間ずっと固定する。
    // 毎フレーム追うと、待機モーションの揺れや振り向きで、カメラまで振り回される。

    // 描画行列の Z 軸（3行目）が前方。
    // 水平成分だけで向きを決める（傾きや見上げ角は構図に入れない）。
    const auto &m  = trans->renderWorldMatrix;
    const float fx = m._31;
    const float fz = m._33;
    if (fx * fx + fz * fz > 1e-8f) cine.shotYaw = std::atan2(fx, fz);
}

EntityID CinematicCameraSystem::FindEntityByName(const std::string &name) const
{
    EntityID found = InvalidEntityID;

    m_world->ViewEach<NameComponent>([&](EntityID id) {
        // すでに見つかっていたら、残りの Entity は無視する
        if (found != InvalidEntityID) return;

        // 名前が一致する Entity を探す
        const auto *nameComp = m_world->GetComponent<NameComponent>(id);
        if (nameComp && name == nameComp->name) found = id;
    });
    return found;
}

bool CinematicCameraSystem::ResolveSubjectPosition(const CinematicCameraComponent &cine, const CameraShot &shot,
                                                   Vector3 &outPos) const
{
    // 基準にするEntityがいない場合は、ワールドの原点を基準にする
    if (shot.subject.empty())
    {
        outPos = Vector3::Zero;
        return true;
    }

    if (!m_world->IsEntityValid(cine.shotSubject)) return false;

    const auto *trans = m_world->GetComponent<TransformComponent>(cine.shotSubject);
    if (!trans) return false;

    // 描画用に補間した位置を使う。
    // 固定ステップの位置をそのまま使うと、描画フレームとずれて対象とカメラの間がガタつく。
    outPos = trans->GetRenderWorldPosition();
    return true;
}

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void CinematicCameraSystem::OnGui()
{
    // --- 台本の選択 ---

    // 選べるのは読み込めた台本だけ。読み直して消えた台本を選んでいたら、先頭に戻す。
    if (m_scripts.find(m_guiScriptPath) == m_scripts.end())
        m_guiScriptPath = m_scripts.empty() ? std::string{} : m_scripts.begin()->first;

    if (ImGui::BeginCombo("台本", m_guiScriptPath.empty() ? "（読み込めた台本がありません）" : m_guiScriptPath.c_str()))
    {
        for (const auto &[path, script] : m_scripts)
        {
            if (ImGui::Selectable(path.c_str(), path == m_guiScriptPath)) m_guiScriptPath = path;
        }
        ImGui::EndCombo();
    }

    // 再生要求は、ゲーム側と同じくイベントで出す。
    // ボタンから Component を直接書き換えると、イベントを受け付ける道を通らずに確認したことになる。
    if (ImGui::Button("再生要求を出す") && !m_guiScriptPath.empty())
        m_world->GetEventBus().Publish(CinematicPlayRequestEvent{m_guiScriptPath});

    // 台本の JSON を書き換えたあと、シーンを開き直さずに確かめるため。
    // 流しているムービーは写した台本で続くので、読み直しの影響は次の再生から。
    ImGui::SameLine();
    if (ImGui::Button("台本を読み直す")) LoadAllScripts();

    // --- 再生の状態 ---
    m_world->ViewEach<CinematicCameraComponent>([&](EntityID id) {
        const auto *cine = m_world->GetComponent<CinematicCameraComponent>(id);
        if (!cine) return;

        ImGui::Text("Entity #%u  %s  %s", EntityHandle::GetIndex(id),
                    kCinematicStateNames[static_cast<int>(cine->state)],
                    cine->script.path.empty() ? "（台本なし）" : cine->script.path.c_str());
        ImGui::Text("%.2f / %.2f 秒  ショット %d / %zu", cine->time, cine->script.duration, cine->activeShot,
                    cine->script.shots.size());
        ImGui::Text("開始待ち %.2f / %.2f 秒  スキップ長押し %.2f / %.2f 秒%s", cine->waitTime,
                    cine->script.startDelay, cine->skipHoldTime, kSkipHoldSeconds,
                    cine->skipArmed ? "" : "（一度離すまで数えない）");
    });
}

// 他のカメラボディと同じステージ。Brain（R01b）より前に姿勢を確定させる。
REGISTER_RENDER_SYSTEM(CinematicCameraSystem, Priority::RenderStage::R01_Camera);
