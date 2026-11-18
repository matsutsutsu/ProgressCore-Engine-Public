#include "Game/Logics/Camera/PlayerCameraSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Core/Math/Interpolation.h"
#include "Engine/Core/Math/Angle.h"

#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/JoltIntegration/IPhysicsAPI.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Time/TimeService.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    // カメラを手で動かしたと判定するしきい値
    // 0 と比べると、スティックのドリフトや手ぶれでロックオンの補正が止まる
    constexpr float kStickThreshold = 0.05f; // スティックの倒し量（-1〜1）
    constexpr float kMouseThreshold = 0.5f;  // マウスの移動量（ピクセル）

    // 頭の高さへの持ち上げを、ないものとして扱う量（0〜1）
    constexpr float kHeadLiftEpsilon = 0.001f;

    // これより短いベクトルは、向きを決められないので使わない（m）
    constexpr float kMinLength = 1e-4f;
}



void PlayerCameraSystem::Initialize()
{
    // 要求をためるだけにして、画角を書き換えるのは Update にする
    // ここで書き換えると、書くのが発行した System になり、申告と食い違う
    ListenEvent<CameraFovPunchRequestEvent>([this](const CameraFovPunchRequestEvent &ev) {
        std::lock_guard lock(m_punchMutex);
        m_pendingPunches.push_back(ev);
    });
}



std::vector<CameraFovPunchRequestEvent> PlayerCameraSystem::TakeFovPunches()
{
    std::lock_guard lock(m_punchMutex);
    return std::exchange(m_pendingPunches, {});
}



// ---------------------------------------------------------------------------
// フレーム単位
// ---------------------------------------------------------------------------

void PlayerCameraSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 届いた要求と、このフレームの視点の入力を読む
    // 2. カメラを1台ずつ更新する
    // 3. カーソルを固定するかを決める

    // ポーズ中は dt を 0 にして、カメラの姿勢を止める
    // 止めないと、止まった世界へカメラだけが追いつこうとする
    // return で抜けないのは、最後のカーソルの固定を外す処理を走らせるため
    dt = Time::PresentationDt(*m_world, dt);

    // ---------------------------------------------------------
    // 1. 要求と入力
    // ---------------------------------------------------------

    // 届いた FOV パンチの要求を取り出す
    // 下で抜けたときも、古い要求を次のフレームへ持ち越さない
    const std::vector<CameraFovPunchRequestEvent> punches = TakeFovPunches();

    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();

    // 視点の入力を読む。カメラが何台あっても共通なので1回だけ
    const FrameInput in = ReadFrameInput(inputAPI);

    // 地形の判定に使う。無ければ地形を避けない
    std::shared_ptr<IPhysicsAPI> physicsAPI =
        m_world->HasResource<std::shared_ptr<IPhysicsAPI>>()
            ? m_world->GetResource<std::shared_ptr<IPhysicsAPI>>()
            : nullptr;

    // ---------------------------------------------------------
    // 2. カメラごとの更新
    // ---------------------------------------------------------

    // 1台でもカーソルの固定を求めたら固定する
    bool wantCursorLock = false;

    ForEachWithID([&](EntityID camID, VirtualCamera &vcam, PlayerCameraComponent &tps, TransformComponent &trans) {
        // このカメラが追う相手への FOV パンチを反映する
        // 画角を一瞬変え、あとは fovDamping で目標の画角へ戻っていく
        for (const CameraFovPunchRequestEvent &punch : punches)
            if (punch.targetID == tps.target.id) vcam.fov = punch.fov;

        // 追従する相手がまだ決まっていないカメラは飛ばす
        // プレハブを作った直後などに起きる
        auto *targetTrans = m_world->GetComponent<TransformComponent>(tps.target.id);
        if (!targetTrans) return;

        // ロックオンの敵を引く。Component が無いカメラはロックオンしない
        // 狙う敵は PlayerLockOnSystem が決める。ここでは消えていたら解除するだけ
        auto                     *lockOn     = m_world->GetComponent<PlayerCameraLockOnComponent>(camID);
        const TransformComponent *enemyTrans = nullptr;
        if (lockOn && lockOn->isActive)
        {
            enemyTrans = m_world->GetComponent<TransformComponent>(lockOn->target.id);
            if (!enemyTrans) lockOn->isActive = false;
        }

        CameraContext c{vcam, tps, lockOn, trans, *targetTrans, enemyTrans, in, physicsAPI.get(), dt};

        // 姿勢を決める。カーソルの固定を求めたら覚えておく
        if (UpdateCamera(c)) wantCursorLock = true;
    });

    // ---------------------------------------------------------
    // 3. カーソルの固定
    // ---------------------------------------------------------

    // カーソルを解放する間は、マウスで視点を回していても固定しない
    if (in.cursorFree) wantCursorLock = false;

    inputAPI->SetMouseLock(wantCursorLock);
}



PlayerCameraSystem::FrameInput
PlayerCameraSystem::ReadFrameInput(const std::shared_ptr<IInputAPI> &inputAPI) const
{
    FrameInput in;

    in.lookX   = inputAPI->GetAxis("LookX");
    in.lookY   = inputAPI->GetAxis("LookY");
    in.mouseDX = inputAPI->GetMouseDeltaX();
    in.mouseDY = inputAPI->GetMouseDeltaY();
    in.rmbHeld = inputAPI->GetAction("FreeLook");

    // どれか1つでもしきい値を超えたら、手で視点を動かしたと判定する
    in.hasManualLook = std::abs(in.lookX) > kStickThreshold ||
                       std::abs(in.lookY) > kStickThreshold ||
                       std::abs(in.mouseDX) > kMouseThreshold ||
                       std::abs(in.mouseDY) > kMouseThreshold;

    const GameFlowState *flow = m_world->HasResource<GameFlowState>() ? &m_world->GetResource<GameFlowState>() : nullptr;

    // カーソルを解放する間（ポーズ・UI 操作・決着の画面）と、
    // エディタでほかのウィンドウを触っている間は、カーソルを固定しない
    in.cursorFree = (flow && flow->CursorFree()) || !inputAPI->IsGameInputActive();

    // ムービー中と決着の後は、視点の入力を受けない
    // 受けると、演出の間に回した分だけ、明けたときにカメラが別の方を向く
    // カーソルの固定は続ける。外すと、ポインタが窓の外へ出る
    in.blockLook = flow && flow->GameplayFrozen();

    return in;
}



bool PlayerCameraSystem::UpdateCamera(CameraContext &c) const
{
    // 演出の上書きを、1つの足し込み量へまとめる
    ResolveFraming(c);

    // 手で動かした分とロックオンの補正で、ヨーとピッチを決める
    UpdateOrbitAngles(c);

    // 注視点と回り込みの中心を追従させ、地形を考える前の距離を出す
    const float baseDistance = UpdateFraming(c);

    // 上書きの角度は見る向きにだけ足し、currentYaw などは書き換えない
    // 書き換えないので、上書きが外れれば普段の構図へそのまま戻る
    const float viewYaw = c.tps.currentYaw + c.framing.yawOffset;

    // 足した後のピッチも制限の中に収める。演出で地面へ潜らないようにする
    const float   viewPitch = std::clamp(c.tps.currentPitch + c.framing.pitchOffset, c.tps.minPitch, c.tps.maxPitch);
    const Vector3 viewDir   = CameraViewDir(viewYaw, viewPitch);

    // 地形を避けて、カメラを置く位置を決める
    const Vector3 finalPos = ResolveCameraPosition(c, viewDir, baseDistance);

    // 画角を進め、Transform と VirtualCamera へ書き出す
    WriteCameraResult(c, finalPos, viewDir);

    // マウスで視点を回す間は、カーソルの固定を求める
    // いつもマウスで回すならいつも、右クリックで回すなら押している間だけ
    return c.tps.enableCursorLock && (!c.tps.mouseLookRequiresButton || c.in.rmbHeld);
}



void PlayerCameraSystem::ResolveFraming(CameraContext &c) const
{
    // 足し込み量は weight を掛けて足すだけなので、合成の順番によらない
    CameraFramingParams sum{};
    bool                movesDistance = false;

    c.tps.framingOverrides.ForEachActive([&](const CameraFramingParams &p, float w) {
        // 距離を外へ戻す速さを変えるのは、距離を動かす上書きのときだけ
        // 画角だけの上書きで変えると、壁で寄ったカメラがその間だけ速く戻る
        if (p.distanceOffset != 0.0f) movesDistance = true;

        sum.distanceOffset += p.distanceOffset * w;
        sum.fovOffset += p.fovOffset * w;
        sum.pitchOffset += p.pitchOffset * w;
        sum.yawOffset += p.yawOffset * w;
        sum.roll += p.roll * w;
        sum.followBoost += p.followBoost * w;

        // 寄せ先は1つしか持てないので、後に重なる方で上書きする
        if (p.focusTarget != InvalidEntityID && p.focus > 0.0f)
        {
            sum.focusTarget = p.focusTarget;
            sum.focus       = std::clamp(p.focus * w, 0.0f, 1.0f);
        }
    });

    c.framing              = sum;
    c.framingMovesDistance = movesDistance;
}



// ---------------------------------------------------------------------------
// どの向きから見るか
// ---------------------------------------------------------------------------

void PlayerCameraSystem::UpdateOrbitAngles(CameraContext &c) const
{
    // 同じ currentYaw / currentPitch を、上から順に書き換えていく
    // 並べ替えると挙動が変わる
    auto &tps = c.tps;

    // 手で動かした分を足す。カットシーン中は受けない
    // ロックオン中も手で回せる。ロックオンは補助で、カメラを奪わない
    if ((!c.enemy || c.lockOn->allowManualCamera) && !c.in.blockLook)
    {
        ApplyManualLook(c);
    }

    // 手で動かしている間と、やめてからしばらくは、補正を止める
    // 手を離した瞬間に敵へ引き戻されると、操作を奪われたように感じる
    if (c.lockOn)
    {
        if (c.in.hasManualLook)
        {
            c.lockOn->manualOverrideTimer = c.lockOn->manualOverrideTime;
        }
        else
        {
            c.lockOn->manualOverrideTimer = std::max(0.0f, c.lockOn->manualOverrideTimer - c.dt);
        }
    }

    // ロックオン中は、ヨーを敵の方へ回し、ピッチを見上げ角へ寄せる
    if (c.enemy && c.lockOn->manualOverrideTimer <= 0.0f)
    {
        ApplyLockOnYawAssist(c);
        ApplyLockOnPitchAssist(c);
    }

    // 手の入力も補正も足し終えてから、ピッチを制限する
    tps.currentPitch = std::clamp(tps.currentPitch, tps.minPitch, tps.maxPitch);

    // ヨーを ±360 度の中に畳む
    // 畳まないと周回のたびに値が増え、float の桁が足りずに視点がカクつく
    // 補正は近い回り方で回すので、ここで角度が飛んでも動きは変わらない
    tps.currentYaw = std::fmod(tps.currentYaw, 360.0f);
}



void PlayerCameraSystem::ApplyManualLook(CameraContext &c) const
{
    auto       &tps = c.tps;
    const auto &in  = c.in;

    // 右スティックは速さの入力なので dt を掛ける
    tps.currentYaw -= in.lookX * tps.lookSpeedX * tps.sensitivityScale * c.dt;
    tps.currentPitch += in.lookY * tps.lookSpeedY * tps.sensitivityScale * c.dt;

    // マウスは1フレームの移動量なので dt を掛けない
    if ((!tps.mouseLookRequiresButton || in.rmbHeld) && !in.cursorFree)
    {
        const float invert = tps.mouseInvertY ? -1.0f : 1.0f;

        tps.currentYaw -= in.mouseDX * tps.mouseSensitivityX * tps.sensitivityScale;
        tps.currentPitch += (-in.mouseDY) * tps.mouseSensitivityY * tps.sensitivityScale * invert;
    }
}



void PlayerCameraSystem::ApplyLockOnYawAssist(CameraContext &c) const
{
    auto       &tps    = c.tps;
    const auto &lockOn = *c.lockOn;

    const Vector3 playerToEnemy = Vector3(c.enemy->GetRenderWorldPosition()) -
                                  Vector3(c.target.GetRenderWorldPosition());

    // 敵の方を見るヨーと、今のヨーとの差（-180〜180 度）
    const float idealYawDeg = CameraYawFromViewDir(playerToEnemy);
    const float yawDiff     = CCL::Math::WrapDegrees(idealYawDeg - tps.currentYaw);

    // 敵がデッドゾーンの中にいれば動かさない
    if (std::abs(yawDiff) <= lockOn.yawDeadZone) return;

    // はみ出た角度に比例した速さで、敵のいる側へ回す
    // 大きく外れていれば速く、縁では穏やかに追う。速さには上限がある
    const float excess = std::abs(yawDiff) - lockOn.yawDeadZone;

    float correctionSpeed = excess * lockOn.assistStrength;

    correctionSpeed = std::min(correctionSpeed, lockOn.maxCorrectionSpeed);

    const float correction = correctionSpeed * c.dt;

    tps.currentYaw += (yawDiff < 0.0f) ? -correction : correction;
}



void PlayerCameraSystem::ApplyLockOnPitchAssist(CameraContext &c) const
{
    // ロックオン中はピッチを pitch へ寄せ続け、カメラを低く構える
    // 開始時に1回だけ寄せる形だと、スティックに触れた瞬間に外れて戻らない
    auto       &tps    = c.tps;
    const auto &lockOn = *c.lockOn;

    const float pitchBlend = CCL::Math::Damp(lockOn.pitchDamping, c.dt);

    tps.currentPitch = std::lerp(tps.currentPitch, lockOn.pitch, pitchBlend);
}



// ---------------------------------------------------------------------------
// どこを中心に、どこへ置くか
// ---------------------------------------------------------------------------

float PlayerCameraSystem::UpdateFraming(CameraContext &c) const
{
    // 処理フロー:
    // 1. プレイヤーの位置から、目標の注視点・中心・距離を出す
    // 2. ロックオン中は敵側へ寄せて引き、普段は進む向きを先に見る
    // 3. 演出の上書きを足す
    // 4. 注視点と中心を、目標へ追従させる

    auto &tps = c.tps;

    // ---------------------------------------------------------
    // 1. 目標
    // ---------------------------------------------------------

    // 補間した描画用の位置を使う。position は補間していない
    const Vector3 pivot = Vector3(c.target.GetRenderWorldPosition()) + tps.targetOffset;

    // ここから下の3つが、このフレームの目標。補間はしていない
    Vector3 goalLookAt   = pivot;
    Vector3 goalPivot    = pivot;
    float   goalDistance = tps.distance;

    // ---------------------------------------------------------
    // 2. ロックオンとルックアヘッド
    // ---------------------------------------------------------

    if (c.enemy)
    {
        // ロックオン中は、注視点を敵側へ寄せる
        // ずらす量に上限を付けて、プレイヤーを画面に残す
        const auto   &lockOn    = *c.lockOn;
        const Vector3 enemyCore = Vector3(c.enemy->GetRenderWorldPosition()) + tps.targetOffset;

        Vector3     lookShift = (enemyCore - pivot) * lockOn.subjugationRatio;
        const float shiftLen  = lookShift.Length();
        if (shiftLen > lockOn.maxLookShift && shiftLen > kMinLength)
            lookShift *= (lockOn.maxLookShift / shiftLen);

        // 回り込みの中心はプレイヤーのままにして、敵が離れるほど引く
        // 中間を中心にすると、敵が遠いときにカメラがプレイヤーより前に出る
        const float sep = (enemyCore - pivot).Length();

        goalLookAt = pivot + lookShift;
        goalDistance += std::clamp(sep * lockOn.distanceGain, 0.0f, lockOn.maxExtraDistance);
    }
    else if (auto *movement = m_world->GetComponent<CharacterMovementComponent>(tps.target.id))
    {
        // 普段は、進む向きへ注視点をずらし、その点を中心に回り込む
        // ロックオン中に掛けると、動くたびに注視点が敵から外れる
        Vector3 vel = movement->currentVelocity;
        vel.y       = 0.0f;

        const Vector3 lookaheadGoal = vel * tps.lookaheadMultiplier;
        const float   laBlend       = CCL::Math::Damp(tps.lookaheadDamping, c.dt);

        tps.currentLookahead = Vector3::Lerp(tps.currentLookahead, lookaheadGoal, laBlend);

        goalLookAt += tps.currentLookahead;
        goalPivot = goalLookAt;
    }

    // ---------------------------------------------------------
    // 3. 演出の上書き
    // ---------------------------------------------------------

    // 距離を足す。寄りすぎてキャラに潜らないよう、衝突の最小距離で止める
    goalDistance = (std::max)(goalDistance + c.framing.distanceOffset, tps.collisionMinDistance);

    // 注視点だけを寄せ先へずらし、回り込みの中心は動かさない
    // 立ち位置は保ったまま、視線だけが敵の方を向く。ずらす量には上限がある
    if (c.framing.focus > 0.0f)
    {
        if (const auto *focusTrans = m_world->GetComponent<TransformComponent>(c.framing.focusTarget))
        {
            const Vector3 focusCore = Vector3(focusTrans->GetRenderWorldPosition()) + tps.targetOffset;

            Vector3     shift    = (focusCore - goalLookAt) * c.framing.focus;
            const float shiftLen = shift.Length();
            if (shiftLen > tps.framingMaxLookShift && shiftLen > kMinLength)
            {
                shift *= (tps.framingMaxLookShift / shiftLen);
            }
            goalLookAt += shift;
        }
    }

    // ---------------------------------------------------------
    // 4. 追従
    // ---------------------------------------------------------

    // 最初のフレームは補間せず、目標へ置く
    if (tps.isFirstFrame)
    {
        tps.currentLookAt      = goalLookAt;
        tps.currentPivot       = goalPivot;
        tps.currentCamDistance = goalDistance;
        c.vcam.fov             = tps.baseFov;
        tps.isFirstFrame       = false;
    }

    // 目標へ指数減衰で追従する。上書きの followBoost の分だけ速める
    // 速めないと、突進するキャラが画面の中で遅れて小さくなる
    const float posBlend = CCL::Math::Damp(tps.positionDamping + (std::max)(c.framing.followBoost, 0.0f), c.dt);

    tps.currentLookAt = Vector3::Lerp(tps.currentLookAt, goalLookAt, posBlend);
    tps.currentPivot  = Vector3::Lerp(tps.currentPivot, goalPivot, posBlend);

    // 距離は補間せずに返す。地形を見てから ResolveCameraPosition が補間する
    return goalDistance;
}



Vector3 PlayerCameraSystem::ResolveCameraPosition(CameraContext &c, const Vector3 &viewDir,
                                                  float desiredDistance) const
{
    // 処理フロー:
    // 1. 中心からカメラへレイを飛ばし、地形があれば手前へ寄せる
    // 2. 寄せる量を補間して、カメラの位置を出す
    // 3. 寄った分だけ、頭の高さへ持ち上げる
    // 4. 持ち上げた先も、地形に入っていないか確かめる

    // 見るのは地形だけ。間をドローンやボスが横切ってもカメラは寄らない
    auto &tps = c.tps;

    // ---------------------------------------------------------
    // 1. 地形の判定
    // ---------------------------------------------------------

    float resolvedDist = desiredDistance;
    bool  blocked      = false; // 地形に当たって寄せたか
    if (tps.collisionEnabled && c.physics)
    {
        const Vector3  castDir = -viewDir; // 中心からカメラへ向かう向き
        const float    maxDist = desiredDistance + tps.collisionRadius;
        XMFLOAT3       o       = {tps.currentPivot.x, tps.currentPivot.y, tps.currentPivot.z};
        XMFLOAT3       d       = {castDir.x, castDir.y, castDir.z};
        PhysicsHitInfo hit     = c.physics->RayCastStatic(o, d, maxDist);
        if (hit.hit)
        {
            // 当たった面から余白だけ手前に置き、めり込まないようにする
            resolvedDist = hit.distance - tps.collisionRadius;
            resolvedDist = std::clamp(resolvedDist, tps.collisionMinDistance, desiredDistance);
            blocked      = true;
        }
    }

    // ---------------------------------------------------------
    // 2. 位置
    // ---------------------------------------------------------

    // 寄せるのは速く、戻すのはゆっくり補間する
    // 距離を動かす上書きの間で地形に当たっていなければ、外へも速く広げる
    // 演出が自分でイーズしているので、ここで遅らせると引きが間に合わない
    float colDamping = tps.collisionInDamping;
    if (resolvedDist >= tps.currentCamDistance)
    {
        colDamping = (c.framingMovesDistance && !blocked)
                         ? (std::max)(tps.collisionOutDamping, tps.framingFollowDamping)
                         : tps.collisionOutDamping;
    }
    const float colBlend = CCL::Math::Damp(colDamping, c.dt);

    tps.currentCamDistance = std::lerp(tps.currentCamDistance, resolvedDist, colBlend);

    Vector3 finalPos = tps.currentPivot - (viewDir * tps.currentCamDistance);

    // ---------------------------------------------------------
    // 3. 頭の高さへの持ち上げ
    // ---------------------------------------------------------

    // 寄った割合に応じて、カメラの高さを中心（頭）の高さへ寄せる
    // 壁や床で寄っても、視点が足元に落ちないようにする
    const float range     = (std::max)(0.001f, desiredDistance - tps.collisionMinDistance);
    const float pullRatio = std::clamp((desiredDistance - tps.currentCamDistance) / range, 0.0f, 1.0f);
    const float liftAmount = pullRatio * tps.collisionHeadBias;

    finalPos.y = std::lerp(finalPos.y, tps.currentPivot.y, liftAmount);

    // ---------------------------------------------------------
    // 4. 持ち上げた先の判定
    // ---------------------------------------------------------

    // 持ち上げがほぼないなら、1 と同じ向きへ2回撃つだけなので省く
    if (tps.collisionEnabled && c.physics && liftAmount > kHeadLiftEpsilon)
    {
        finalPos = ResolveHeadLiftCollision(c, finalPos);
    }

    return finalPos;
}



Vector3 PlayerCameraSystem::ResolveHeadLiftCollision(const CameraContext &c,
                                                        const Vector3 &liftedPos) const
{
    // 持ち上げた先は、1回目のレイの通り道から外れている
    // 庇や階段の裏ではまた地形に入るので、もう1回確かめる
    const auto &tps = c.tps;

    const Vector3 toCam    = liftedPos - tps.currentPivot;
    const float   liftDist = toCam.Length();
    if (liftDist <= kMinLength) return liftedPos;

    const Vector3  liftDir = toCam / liftDist;
    XMFLOAT3       o       = {tps.currentPivot.x, tps.currentPivot.y, tps.currentPivot.z};
    XMFLOAT3       d       = {liftDir.x, liftDir.y, liftDir.z};
    PhysicsHitInfo hit     = c.physics->RayCastStatic(o, d, liftDist + tps.collisionRadius);
    if (!hit.hit) return liftedPos;

    const float safeDist =
        std::clamp(hit.distance - tps.collisionRadius, tps.collisionMinDistance, liftDist);

    return tps.currentPivot + liftDir * safeDist;
}



// ---------------------------------------------------------------------------
// 出力
// ---------------------------------------------------------------------------

void PlayerCameraSystem::WriteCameraResult(CameraContext &c, const Vector3 &finalPos,
                                           const Vector3 &viewDir) const
{
    // 画角を、普段の画角に上書きの足し込みを足した値へ寄せる
    const float targetFov = c.tps.baseFov + c.framing.fovOffset;
    const float fovBlend  = CCL::Math::Damp(c.tps.fovDamping, c.dt);

    c.vcam.fov = std::lerp(c.vcam.fov, targetFov, fovBlend);

    // 上書きの傾き（roll）があれば、視線の軸まわりに上方向を回す
    Vector3 up = Vector3::Up;
    if (std::abs(c.framing.roll) > 0.001f)
    {
        Vector3 rollAxis = c.tps.currentLookAt - finalPos;
        if (rollAxis.LengthSquared() > kMinLength * kMinLength)
        {
            rollAxis.Normalize();
            const Quaternion rollRot = Quaternion::CreateFromAxisAngle(rollAxis, XMConvertToRadians(c.framing.roll));
            up                       = Vector3::Transform(Vector3::Up, rollRot);
        }
    }

    // カメラ自身の Transform。ギズモやデバッグ表示が読む
    c.trans.position = finalPos;

    const XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), viewDir, up);
    XMVECTOR       det;
    const XMMATRIX worldRot = XMMatrixInverse(&det, lookMat);

    c.trans.rotation = Quaternion::CreateFromRotationMatrix(worldRot);

    // CameraBrainSystem が読む、このカメラの結果
    c.vcam.resultPos    = finalPos;
    c.vcam.resultLookAt = c.tps.currentLookAt;
    c.vcam.resultUp     = up;

    // 位置と注視点の両方を決めたので、Brain に採用していいと伝える
    c.vcam.MarkResolved();
}



// CameraBrainSystem（R01b）が採用する前に、姿勢を決める
REGISTER_RENDER_SYSTEM(PlayerCameraSystem, Priority::RenderStage::R01_Camera);
