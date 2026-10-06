#include "Engine/Camera/Systems/CameraActionTPSSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Camera/CameraEvents.h"
#include "Engine/Core/Math/Interpolation.h" // Damp(): フレームレート非依存の追従係数

#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/JoltIntegration/IPhysicsAPI.h" // カメラ衝突（地形レイキャスト）
#include "Engine/Core/App/GameFlowState.h"              // ポーズ中はカーソルロックを解除するため
#include "Engine/Time/TimeService.h"                    // Time::PresentationDt（ポーズ中は追従を止める）

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
// 「プレイヤーがカメラを操作した」と見なすしきい値。
// 0 と比べると、スティックのドリフトや数ピクセルの手ぶれでロックオン補正が止まる。
constexpr float kStickThreshold = 0.05f; // スティック倒し量（-1..1）
constexpr float kMouseThreshold = 0.5f;  // マウス移動量（ピクセル/フレーム）

// 頭の高さへの持ち上げが「実質ゼロ」と見なせる量（0..1 の比率）。
constexpr float kHeadLiftEpsilon = 0.001f;

// 長さがこれ以下のベクトルは向きを論じられないものとして扱う（m）。
constexpr float kMinLength = 1e-4f;
} // namespace

void CameraActionTPSSystem::Initialize()
{
    // 画角の瞬間的な変化(FOVパンチ)要求を受信する。
    ListenEvent<CameraFovPunchRequestEvent>([this](const CameraFovPunchRequestEvent &ev) {
        ForEach([&](VirtualCamera &vcam, const CameraBodyTPSComponent &tps, TransformComponent &trans) {
            if (tps.target.id == ev.targetID)
            {
                // 画角を瞬間的に変える。次のフレームから Update の fovDamping による
                // 指数減衰で目標の画角へ戻る。
                vcam.fov = ev.fov;
            }
        });
    });
}

// ---------------------------------------------------------------------------
// フレーム単位
// ---------------------------------------------------------------------------

void CameraActionTPSSystem::Update(float dt)
{
    // 描画ステージなのでポーズ中も走る。実時間 dt のまま減衰を進めると、止まった世界へ
    // カメラだけが追いつこうとして動くので、ポーズ中は dt=0 で回して姿勢を凍結する。
    // return で抜けないのは、末尾のカーソルロック解除（ポーズメニュー用）を走らせるため。
    dt = Time::PresentationDt(*m_world, dt);

    // 1. 今のフレームの視点入力と、入力を無効にする条件を確定する
    //    カメラが何台あっても入力は共通なので、ここで1度だけ読む。
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();

    const FrameInput in = ReadFrameInput(inputAPI);

    // カメラ衝突用の物理API（地形とだけ交差判定する）。無ければ衝突回避はスキップ。
    std::shared_ptr<IPhysicsAPI> physicsAPI =
        m_world->HasResource<std::shared_ptr<IPhysicsAPI>>()
            ? m_world->GetResource<std::shared_ptr<IPhysicsAPI>>()
            : nullptr;

    // 2. TPSカメラを1台ずつ更新する
    //    戻り値は「このカメラがカーソル固定を要求するか」。1台でも要求すれば固定する。
    bool wantCursorLock = false;

    ForEach([&](VirtualCamera &vcam, CameraBodyTPSComponent &tps, TransformComponent &trans) {
        // 追従対象。R00b の CameraTargetBindSystem が解決済み。まだ結び付いていない
        // フレーム（プレハブ生成直後など）は、このカメラを何もせず飛ばす。
        auto *targetTrans = m_world->GetComponent<TransformComponent>(tps.target.id);
        if (!targetTrans) return;

        // ロックオン対象。誰を狙うか・いつ解除するかはゲーム側(PlayerLockOnSystem)が
        // 決めるので、ここでは判断しない。対象が消滅していたときだけ、描画側の保険として
        // 旗を倒し、以降を通常時と同じ扱いにする。
        const TransformComponent *enemyTrans = nullptr;
        if (tps.isLockOnActive)
        {
            enemyTrans = m_world->GetComponent<TransformComponent>(tps.lockOnTarget.id);
            if (!enemyTrans) tps.isLockOnActive = false;
        }

        CameraContext c{vcam, tps, trans, *targetTrans, enemyTrans, in, physicsAPI.get(), dt};

        if (UpdateCamera(c))
            wantCursorLock = true;
    });

    // 3. 全カメラの要求を1つの結論に集約して、カーソルロックを決める
    //    マウスルック中でも cursorFree（ポーズ中・UI 操作中・エディタで他ウィンドウ操作中）
    //    なら固定しない。さらに Mouse 側がウィンドウ非フォーカス時にロックを無効にするので、
    //    alt-tab でカーソルが閉じ込められることもない。
    if (in.cursorFree)
        wantCursorLock = false;

    inputAPI->SetMouseLock(wantCursorLock);
}

CameraActionTPSSystem::FrameInput
CameraActionTPSSystem::ReadFrameInput(const std::shared_ptr<IInputAPI> &inputAPI) const
{
    FrameInput in;

    in.lookX   = inputAPI->GetAxis("LookX");
    in.lookY   = inputAPI->GetAxis("LookY");
    in.mouseDX = inputAPI->GetMouseDeltaX();
    in.mouseDY = inputAPI->GetMouseDeltaY();
    in.rmbHeld = inputAPI->GetAction("FreeLook");

    in.hasManualLook = std::abs(in.lookX) > kStickThreshold ||
                       std::abs(in.lookY) > kStickThreshold ||
                       std::abs(in.mouseDX) > kMouseThreshold ||
                       std::abs(in.mouseDY) > kMouseThreshold;

    // カーソルを固定しない（マウスルックも止める）条件。
    //   ・ポーズ中（メニュー操作にカーソルが要る）
    //   ・カーソル自由(UI操作)モード中（実行中に ImGui エディタを操作したい）
    //   ・エディタで他ウィンドウ操作中（IsGameInputActive=false）
    const bool flowPaused =
        m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().paused;
    const bool flowUiInteract =
        m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().uiInteract;

    in.cursorFree = flowPaused || flowUiInteract || !inputAPI->IsGameInputActive();

    // 導入カメラ演出などのカットシーン中は視点入力を一切受けない。
    //   ここを塞がないと、演出を見ている5秒の間にスティック/マウスを動かした分だけ
    //   currentYaw が回り続け、演出明けにカメラが明後日を向く。
    //   PlayerInputSystem は cinematic で入力を落としているが、本システムは
    //   IInputAPI を直接読むので同じ条件がここにも要る。
    //   カーソルロックは維持する（解放するとフルスクリーンでポインタが窓外へ逃げる）。
    in.blockLook =
        m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().cinematic;

    return in;
}

bool CameraActionTPSSystem::UpdateCamera(CameraContext &c) const
{
    // 【備考】
    //   ロックオンの処理は1箇所にまとまっていない。カメラの向きは 1、画面の構図は 2 が持つ。
    //   狙う相手の決定と解除は本システムの外（ゲーム側の PlayerLockOnSystem）の役割。

    // 1. どの向きから見るかを決める
    //    手動入力 → 補正の一時停止判定 → ロックオン補正 → 制限、の順に
    //    currentYaw / currentPitch を上書きしていく。
    UpdateOrbitAngles(c);

    // 2. どこを中心に、どれだけ離れて見るかを決める
    //    ロックオン中は注視点を敵側へ寄せ、中心はプレイヤー基準にし、離れるほど引く。
    //    通常時は進行方向へのルックアヘッドを足す。
    const float baseDistance = UpdateFraming(c);

    // 3. 1 の角度を方向ベクトルに直し、2 の距離から地形を避けた位置を出す
    const Vector3 backwardDir = ComputeBackwardDir(c.tps.currentYaw, c.tps.currentPitch);
    const Vector3 finalPos    = ResolveCameraPosition(c, backwardDir, baseDistance);

    // 4. 画角を進め、Transform と VirtualCamera へ書き出す
    WriteCameraResult(c, finalPos, backwardDir);

    // 5. マウスルック中ならカーソル固定を要求する（最終判断は呼び出し側）
    //    常時マウス視点なら常に、右クリック視点なら押下中のみ。
    return c.tps.enableCursorLock && (!c.tps.mouseLookRequiresButton || c.in.rmbHeld);
}

// ---------------------------------------------------------------------------
// どの向きから見るか（オービット角）
// ---------------------------------------------------------------------------

void CameraActionTPSSystem::UpdateOrbitAngles(CameraContext &c) const
{
    // 【備考】
    //   同じ currentYaw / currentPitch を上から順に上書きしていく。
    //   順序そのものが仕様なので、並べ替えると挙動が変わる。

    auto &tps = c.tps;

    // 1. 手動の視点入力を積む
    //    ロックオン中も手動操作を許す（lockOnAllowManualCamera）。ロックオンはカメラを
    //    奪うのではなく、補助するだけにする。カットシーン中は一切受けない。
    if ((!c.enemy || tps.lockOnAllowManualCamera) && !c.in.blockLook)
    {
        ApplyManualLook(c);
    }

    // 2. 手動操作があった間はロックオン自動補正を止める
    //    操作をやめた後も lockOnManualOverrideTime の間は止めたままにする。
    //    手を離した瞬間にカメラが敵へ引き戻されると、操作を奪われた感じになる。
    if (c.in.hasManualLook)
    {
        tps.lockOnManualOverrideTimer = tps.lockOnManualOverrideTime;
    }
    else
    {
        tps.lockOnManualOverrideTimer = std::max(0.0f, tps.lockOnManualOverrideTimer - c.dt);
    }

    // 3. ロックオン中は、敵の方向（ヨー）と見上げ角（ピッチ）を自動補正する
    //    2 のタイマーが残っている間は走らない（手動操作を優先する）。
    if (c.enemy && tps.lockOnManualOverrideTimer <= 0.0f)
    {
        ApplyLockOnYawAssist(c);
        ApplyLockOnPitchAssist(c);
    }

    // 4. ピッチを制限する
    //    手動入力も自動補正も通した後に掛ける。ここが角度の最終的な門。
    tps.currentPitch = std::clamp(tps.currentPitch, tps.minPitch, tps.maxPitch);

    // 5. ヨーを ±360 に畳む
    //    周回するたび無限に積算されるので、放置すると float の有効桁が回転量に食われ、
    //    長時間プレイで視点がカクつくようになる（プレハブに -4572 度が保存されていた）。
    //    3 のヨー補正は yawDiff で最短回転を取っているため、ここで角度が飛んでも
    //    挙動は変わらない。
    tps.currentYaw = std::fmod(tps.currentYaw, 360.0f);
}

void CameraActionTPSSystem::ApplyManualLook(CameraContext &c) const
{
    auto       &tps = c.tps;
    const auto &in  = c.in;

    // ゲームパッドの右スティックは速度入力なので dt を掛ける。
    tps.currentYaw -= in.lookX * tps.lookSpeedX * tps.sensitivityScale * c.dt;
    tps.currentPitch += in.lookY * tps.lookSpeedY * tps.sensitivityScale * c.dt;

    // マウスは1フレームの移動量なので dt を掛けない。
    if ((!tps.mouseLookRequiresButton || in.rmbHeld) && !in.cursorFree)
    {
        const float invert = tps.mouseInvertY ? -1.0f : 1.0f;

        tps.currentYaw -= in.mouseDX * tps.mouseSensitivityX * tps.sensitivityScale;
        tps.currentPitch += (-in.mouseDY) * tps.mouseSensitivityY * tps.sensitivityScale * invert;
    }
}

void CameraActionTPSSystem::ApplyLockOnYawAssist(CameraContext &c) const
{
    auto &tps = c.tps;

    const Vector3 playerToEnemy = Vector3(c.enemy->GetRenderWorldPosition()) -
                                  Vector3(c.target.GetRenderWorldPosition());

    // 敵の方向をヨー角に変換する。
    const float idealYawDeg = XMConvertToDegrees(atan2f(playerToEnemy.z, playerToEnemy.x));

    // 現在のカメラのヨーとの差を -180 ～ +180 に正規化する。
    float yawDiff = idealYawDeg - tps.currentYaw;

    while (yawDiff > 180.0f)
        yawDiff -= 360.0f;

    while (yawDiff < -180.0f)
        yawDiff += 360.0f;

    // 敵がデッドゾーン内に収まっているならカメラを動かさない。
    if (std::abs(yawDiff) <= tps.lockOnYawDeadZone) return;

    // デッドゾーンを超えた量に比例して補正速度を上げ、上限で抑える。
    // 大きく外れていれば速く、縁では穏やかに追う。
    const float excess = std::abs(yawDiff) - tps.lockOnYawDeadZone;

    float correctionSpeed = excess * tps.lockOnAssistStrength;

    correctionSpeed = std::min(correctionSpeed, tps.lockOnMaxCorrectionSpeed);

    // 度/秒 → 今フレームの角度へ。敵のいる側へ回す。
    const float correction = correctionSpeed * c.dt;

    tps.currentYaw += (yawDiff < 0.0f) ? -correction : correction;
}

void CameraActionTPSSystem::ApplyLockOnPitchAssist(CameraContext &c) const
{
    // ロックオン中は見上げ角を lockOnPitch へ寄せ続け、カメラを低く構えさせる。
    //   ドローンが上空から攻めてくるので、戦闘中は空が画面に入っている必要がある。
    //   カメラを下げると、注視点（プレイヤー〜ボス）は動かないまま視線が上を向くので、
    //   同じ構図のまま頭上の空間だけが広がる。
    //
    //   開始時に一度だけ寄せる形にはしない。それだとプレイヤーがスティックに触れた
    //   瞬間に解除され、以降は見下ろしのまま戻らず「ロックオン中は低い」という
    //   状態にならない。手動操作の直後は呼び出し側のタイマーで止まるので、
    //   自分で上下を見回す余地は残る。
    auto &tps = c.tps;

    const float pitchBlend = CCL::Math::Damp(tps.lockOnPitchDamping, c.dt);

    tps.currentPitch = std::lerp(tps.currentPitch, tps.lockOnPitch, pitchBlend);
}

// ---------------------------------------------------------------------------
// どこを中心に、どこへ置くか
// ---------------------------------------------------------------------------

float CameraActionTPSSystem::UpdateFraming(CameraContext &c) const
{
    auto &tps = c.tps;

    // 1. プレイヤー基準の注視点と基準距離を出す
    //    position（非補間）ではなく renderWorldMatrix の平行移動成分（補間済み）を使う。
    //    寄り要求中（切り払い準備など）は基準距離を closeUpDistance へ寄せる。実際の
    //    寄り/戻りは currentCamDistance の指数減衰で滑らかに補間される。
    const Vector3 pivot = Vector3(c.target.GetRenderWorldPosition()) + tps.targetOffset;

    // ここから下の3つが「このフレームの目標」。補間はしていない。
    Vector3 goalLookAt   = pivot;
    Vector3 goalPivot    = pivot;
    float   goalDistance = tps.closeUpRequest ? tps.closeUpDistance : tps.distance;

    if (c.enemy)
    {
        // 2a. ロックオン中のフレーミング（遠距離でプレイヤーが画面外に出るのを防ぐ）:
        //   (1) 注視点はプレイヤー→敵へ寄せるが、ずれ量に上限(lockOnMaxLookShift)を設け、
        //       遠距離でもプレイヤーを必ずフレーム内に残す。
        //   (2) 回り込み中心はプレイヤー基準のまま。注視点（中間地点）を中心に引くと、
        //       敵が遠いときカメラがプレイヤーより前に出て、プレイヤーが画面外になる。
        //   (3) 敵が離れるほどカメラを引き、プレイヤーと敵の両方をフレームに収める。
        const Vector3 enemyCore = Vector3(c.enemy->GetRenderWorldPosition()) + tps.targetOffset;

        Vector3     lookShift = (enemyCore - pivot) * tps.lockOnSubjugationRatio;
        const float shiftLen  = lookShift.Length();
        if (shiftLen > tps.lockOnMaxLookShift && shiftLen > kMinLength)
            lookShift *= (tps.lockOnMaxLookShift / shiftLen); // プレイヤーを画面内に保つクランプ

        const float sep = (enemyCore - pivot).Length();

        goalLookAt = pivot + lookShift;
        goalDistance += std::clamp(sep * tps.lockOnDistanceGain, 0.0f, tps.lockOnMaxExtraDistance);
    }
    else if (auto *movement = m_world->GetComponent<CharacterMovementComponent>(tps.target.id))
    {
        // 2b. 通常時のみルックアヘッド（進行方向への視点ズレ）を適用する。
        //     ロックオン中に掛けると、移動のたびに注視点が敵から外れてしまう。
        Vector3 vel = movement->currentVelocity;
        vel.y       = 0.0f;

        const Vector3 lookaheadGoal = vel * tps.lookaheadMultiplier;
        const float   laBlend       = CCL::Math::Damp(tps.lookaheadDamping, c.dt);

        tps.currentLookahead = Vector3::Lerp(tps.currentLookahead, lookaheadGoal, laBlend);

        goalLookAt += tps.currentLookahead;
        goalPivot = goalLookAt; // 通常時は注視点を中心に回り込む
    }

    // 3. 初回フレームは補間せず目標へ置く
    if (tps.isFirstFrame)
    {
        tps.currentLookAt      = goalLookAt;
        tps.currentPivot       = goalPivot;
        tps.currentCamDistance = goalDistance;
        c.vcam.fov             = tps.baseFov;
        tps.isFirstFrame       = false;
    }

    // 4. 目標へ指数減衰で追従する
    const float posBlend = CCL::Math::Damp(tps.positionDamping, c.dt);

    tps.currentLookAt = Vector3::Lerp(tps.currentLookAt, goalLookAt, posBlend);
    tps.currentPivot  = Vector3::Lerp(tps.currentPivot, goalPivot, posBlend);

    // 距離だけは補間せず目標値を返す。地形との衝突を見てから、ResolveCameraPosition が
    // 寄せ/戻しの速さを分けて補間する。
    return goalDistance;
}

Vector3 CameraActionTPSSystem::ComputeBackwardDir(float yawDeg, float pitchDeg)
{
    const float pitchRad = XMConvertToRadians(pitchDeg);
    const float yawRad   = XMConvertToRadians(yawDeg);

    Vector3 backwardDir;
    backwardDir.x = cosf(yawRad) * cosf(pitchRad);
    backwardDir.y = sinf(pitchRad);
    backwardDir.z = sinf(yawRad) * cosf(pitchRad);
    backwardDir.Normalize();

    return backwardDir;
}

Vector3 CameraActionTPSSystem::ResolveCameraPosition(CameraContext &c, const Vector3 &backwardDir,
                                                     float desiredDistance) const
{
    // 【備考】
    //   判定するのは地形(NON_MOVING)だけ。間をドローンやボスが横切ってもカメラは寄らない。

    auto &tps = c.tps;

    // 1. 回り込み中心からカメラ本体へ向けて地形をレイキャストし、壁があれば手前へ寄せる
    float resolvedDist = desiredDistance;
    if (tps.collisionEnabled && c.physics)
    {
        const Vector3  castDir = -backwardDir; // pivot からカメラ本体へ向かう向き
        const float    maxDist = desiredDistance + tps.collisionRadius;
        XMFLOAT3       o       = {tps.currentPivot.x, tps.currentPivot.y, tps.currentPivot.z};
        XMFLOAT3       d       = {castDir.x, castDir.y, castDir.z};
        PhysicsHitInfo hit     = c.physics->RayCastStatic(o, d, maxDist);
        if (hit.hit)
        {
            // 壁も床も、当たったら collisionRadius ぶん手前に置いてめり込みを防ぐ。
            resolvedDist = hit.distance - tps.collisionRadius;
            resolvedDist = std::clamp(resolvedDist, tps.collisionMinDistance, desiredDistance);
        }
    }

    // 2. 寄せ量を補間して、カメラ位置を出す
    //    fast-in / slow-out：内側へ寄せるのは素早く、外側へ戻すのはゆっくり補間する。
    const float colDamping =
        (resolvedDist < tps.currentCamDistance) ? tps.collisionInDamping : tps.collisionOutDamping;
    const float colBlend = CCL::Math::Damp(colDamping, c.dt);

    tps.currentCamDistance = std::lerp(tps.currentCamDistance, resolvedDist, colBlend);

    Vector3 finalPos = tps.currentPivot - (backwardDir * tps.currentCamDistance);

    // 3. 寄った分だけ高さを頭付近へ持ち上げる
    //    引き込み量(pullRatio)に応じて、カメラのY(高さ)を pivot(頭)の高さへ寄せる。
    //    これで壁/床でカメラが近づいても視点が足元に落ちず、頭〜肩の高さを保つ。
    const float range     = (std::max)(0.001f, desiredDistance - tps.collisionMinDistance);
    const float pullRatio = std::clamp((desiredDistance - tps.currentCamDistance) / range, 0.0f, 1.0f);
    const float liftAmount = pullRatio * tps.collisionHeadBias;

    finalPos.y = std::lerp(finalPos.y, tps.currentPivot.y, liftAmount);

    // 4. 持ち上げ先も地形を確認する
    //    持ち上げ量が実質ゼロなら、1 と同じ経路を2度撃つだけなので省く。
    if (tps.collisionEnabled && c.physics && liftAmount > kHeadLiftEpsilon)
    {
        finalPos = ResolveHeadLiftCollision(c, finalPos);
    }

    return finalPos;
}

Vector3 CameraActionTPSSystem::ResolveHeadLiftCollision(const CameraContext &c,
                                                        const Vector3 &liftedPos) const
{
    // 持ち上げた先は、距離を決めたときのレイキャストの経路上に無い（Y方向へずらしたため）。
    //   庇や階段裏では、寄せたはずのカメラが持ち上げによって再びジオメトリの
    //   内側へ入ってしまう。確定位置へ向けてもう一度だけ地形を確認し、押し戻す。
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

void CameraActionTPSSystem::WriteCameraResult(CameraContext &c, const Vector3 &finalPos,
                                              const Vector3 &backwardDir) const
{
    // 1. 画角を要求値へ寄せる
    //    「広げたい」という要求はゲーム側(PlayerDashSystem 等)が wideFovRequest へ
    //    書き込む。カメラは理由を知らずに要求だけを見る。
    const float targetFov = c.tps.wideFovRequest ? c.tps.dashFov : c.tps.baseFov;
    const float fovBlend  = CCL::Math::Damp(c.tps.fovDamping, c.dt);

    c.vcam.fov = std::lerp(c.vcam.fov, targetFov, fovBlend);

    // 2. カメラ自身の Transform（ギズモやデバッグ表示が読む）
    c.trans.position = finalPos;

    const XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), backwardDir, Vector3::Up);
    XMVECTOR       det;
    const XMMATRIX worldRot = XMMatrixInverse(&det, lookMat);

    c.trans.rotation = Quaternion::CreateFromRotationMatrix(worldRot);

    // 3. CameraBrainSystem(R01b) が読む「このカメラの答え」
    c.vcam.resultPos    = finalPos;
    c.vcam.resultLookAt = c.tps.currentLookAt;
    c.vcam.resultUp     = Vector3::Up;

    // 位置・注視点とも自前で確定させたので Brain が採用してよい
    c.vcam.MarkResolved();
}

// R01_Camera ステージで姿勢を確定させ、CameraBrainSystem(R01b) がそれを読む
REGISTER_RENDER_SYSTEM(CameraActionTPSSystem, Priority::RenderStage::R01_Camera);
