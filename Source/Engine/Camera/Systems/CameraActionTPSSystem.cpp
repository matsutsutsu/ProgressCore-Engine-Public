/// @file CameraActionTPSSystem.cpp
/// @brief CameraActionTPSSystem のUpdateロジック実装。

#include "Engine/Camera/Systems/CameraActionTPSSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Camera/CameraEvents.h"

#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Physics/JoltIntegration/IPhysicsAPI.h" // カメラ衝突（地形レイキャスト）
#include "Engine/Core/App/GameFlowState.h" // ポーズ中はカーソルロックを解除するため

#include <memory>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

void CameraActionTPSSystem::Initialize()
{
    // 画角の瞬間的な変化(FOVパンチ)要求を受信する。
    // 「なぜ広げたいか」(ジャスト回避など)はゲーム側の関心事なので、ここでは知らない。
    ListenEvent<CameraFovPunchRequestEvent>([this](const CameraFovPunchRequestEvent &ev) {
        // 自分の担当している仮想カメラを走査
        ForEach([&](VirtualCamera &vcam, const CameraBodyTPSComponent &tps, TransformComponent &trans) {
            // 要求されたエンティティを映しているカメラなら、衝撃(FOVパンチ)を与える
            if (tps.target.id == ev.targetID)
            {
                // 画角を瞬間的に広げる。
                // 次のフレームから自動的に「fovDamping」の摩擦力によって滑らかに元に戻る。
                vcam.fov = ev.fov;
            }
        });
    });

    // 着地などの汎用カメラシェイクを受信
    ListenEvent<CameraShakeRequestEvent>([this](const CameraShakeRequestEvent &ev) {

        ForEachWithID([&](EntityID id, VirtualCamera &vcam, CameraBodyTPSComponent &tps, TransformComponent &trans) {
            // CameraShakeComponentがある場合は振動、ない場合はAddComponentして遅延実行で振動
            if (auto *shake = m_world->GetComponent<CameraShake>(id))
            {
                shake->SetShake(ev.amplitude, ev.duration);
            }
            else
            {
                CameraShake newShake;
                newShake.SetShake(ev.amplitude, ev.duration);
                m_world->AddComponent<CameraShake>(id, newShake);
            }
        });
    });

}

void CameraActionTPSSystem::Update(float dt)
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();

    // ゲームパッド右スティック（倒し量：-1..1）。あとで dt を掛けて積分する。
    float lookX = inputAPI->GetAxis("LookX");
    float lookY = inputAPI->GetAxis("LookY");

    // マウスの毎フレーム移動量（ピクセル）。既に「移動量」なので dt は掛けない。
    float mouseDX = inputAPI->GetMouseDeltaX();
    float mouseDY = inputAPI->GetMouseDeltaY();
    bool  rmbHeld = inputAPI->GetAction("FreeLook"); // 右クリック押下中か

    const float cameraInputThreshold = 0.05f;

    bool manualCameraInput =
        std::abs(lookX) > cameraInputThreshold ||
        std::abs(lookY) > cameraInputThreshold ||
        std::abs(mouseDX) > 0.5f ||
        std::abs(mouseDY) > 0.5f;


    // カメラ衝突用の物理API（地形とだけ交差判定する）。無ければ衝突回避はスキップ。
    std::shared_ptr<IPhysicsAPI> physicsAPI =
        m_world->HasResource<std::shared_ptr<IPhysicsAPI>>()
            ? m_world->GetResource<std::shared_ptr<IPhysicsAPI>>()
            : nullptr;

    // 追従対象(プレイヤー)を「役割」で毎フレーム解決しておく。
    //    プレハブ/レベルで別々に配置されても、ここで自動接続されるため
    //    以前のようにハードコードで target を焼き込む必要がない。

    // カーソルを固定しない（＝マウスルックも止める）条件をフレーム先頭で確定。
    //   ・ポーズ中（メニュー操作にカーソルが要る）
    //   ・カーソル自由(UI操作)モード中（実行中に ImGui エディタを操作したい）
    //   ・エディタで他ウィンドウ操作中（IsGameInputActive=false）
    const bool flowPaused =
        m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().paused;
    const bool flowUiInteract =
        m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().uiInteract;
    const bool cursorFreeOverride =
        flowPaused || flowUiInteract || !inputAPI->IsGameInputActive();

    // このフレーム、いずれかのTPSカメラがマウスルック中ならカーソルをロックしたい。
    bool wantCursorLock = false;

    // ForEachは IfSystem のテンプレート引数(3つのコンポーネント)と完全に一致させる
    ForEach([&](VirtualCamera &vcam, CameraBodyTPSComponent &tps, TransformComponent &trans) {
        // ※ target の解決(役割による自動接続)は、ゲーム側の CameraTargetBindSystem が
        //    本システムより前のステージ(R00b)で済ませている。

        // 1. 追従対象（プレイヤー）の取得
        auto *targetTrans = m_world->GetComponent<TransformComponent>(tps.target.id);
        if (!targetTrans) return;

        // このカメラが「今マウスルック中」ならカーソルロックを要求する。
        //   常時マウス視点なら常に、右クリック視点なら押下中のみ。
        if (tps.enableCursorLock && (!tps.mouseLookRequiresButton || rmbHeld))
            wantCursorLock = true;

        // 4. 注視点（LookAt）の算出
        // position（非補間）ではなく renderWorldMatrix の平行移動成分（補間済み）を使う
        Vector3 targetWorldPos = targetTrans->GetRenderWorldPosition();

        Vector3 idealLookAt = targetWorldPos + tps.targetOffset;


        // ===================================================================
        // ロックオン状態(lockOnTarget / isLockOnActive)は、その持ち主である
        // ゲーム側(PlayerLockOnSystem)がこの CameraBodyTPSComponent へ書き込む。
        // 対象の死亡・消滅による解除もそちらが行うため、ここでは判断しない。
        // ===================================================================

        // 2. ロックオン対象（エネミー）のTransform取得
        auto *enemyTrans = tps.isLockOnActive ? m_world->GetComponent<TransformComponent>(tps.lockOnTarget.id) : nullptr;

        // Transform が取れない（＝既に消滅している）場合だけ、描画側の保険として無効化する
        if (tps.isLockOnActive && !enemyTrans)
        {
            tps.isLockOnActive = false;
        }

        // ===================================================================
        // プレイヤーによるカメラ操作
        //
        // ロックオン中でも基本的に操作可能。
        // ロックオンは「カメラを奪う」のではなく「補助する」。
        // ===================================================================

        if (tps.lockOnAllowManualCamera || !enemyTrans)
        {
            // ---------------------------------------------------------------
            // ゲームパッド
            // ---------------------------------------------------------------

            tps.currentYaw -= lookX * tps.lookSpeedX * dt;
            tps.currentPitch += lookY * tps.lookSpeedY * dt;

            // ---------------------------------------------------------------
            // マウス
            // ---------------------------------------------------------------

            if ((!tps.mouseLookRequiresButton || rmbHeld) &&
                !cursorFreeOverride)
            {
                float invert =
                    tps.mouseInvertY ? -1.0f : 1.0f;

                tps.currentYaw -=
                    mouseDX * tps.mouseSensitivityX;

                tps.currentPitch +=
                    (-mouseDY) *
                    tps.mouseSensitivityY *
                    invert;
            }
        }

        // ===================================================================
        // 3. オービット（回転角）の決定
        //
        // ロックオン中もプレイヤーのカメラ操作を許可する。
        // ロックオンは「カメラを奪う」のではなく「カメラを補助する」。
        // ===================================================================

        // ===================================================================
        // 3-1. プレイヤーによる手動カメラ操作
        // ===================================================================

        if (!enemyTrans || tps.lockOnAllowManualCamera)
        {
            // ---------------------------------------------------------------
            // ゲームパッド
            // ---------------------------------------------------------------

            // 右スティックは速度入力なので dt を掛ける。
            tps.currentYaw -=
                lookX * tps.lookSpeedX * dt;

            tps.currentPitch +=
                lookY * tps.lookSpeedY * dt;

            // ---------------------------------------------------------------
            // マウス
            // ---------------------------------------------------------------

            if ((!tps.mouseLookRequiresButton || rmbHeld) &&
                !cursorFreeOverride)
            {
                float invert =
                    tps.mouseInvertY ? -1.0f : 1.0f;

                // マウスは1フレームの移動量なので dt を掛けない。
                tps.currentYaw -=
                    mouseDX * tps.mouseSensitivityX;

                tps.currentPitch +=
                    (-mouseDY) *
                    tps.mouseSensitivityY *
                    invert;
            }
        }

        // ===================================================================
        // 3-2. プレイヤーがカメラを操作したか判定
        // ===================================================================

        const float stickThreshold = 0.05f;
        const float mouseThreshold = 0.5f;

        const bool manualCameraInput =
            std::abs(lookX) > stickThreshold ||
            std::abs(lookY) > stickThreshold ||
            std::abs(mouseDX) > mouseThreshold ||
            std::abs(mouseDY) > mouseThreshold;

        // ===================================================================
        // 3-3. 手動操作中はロックオン自動補正を一時停止
        // ===================================================================

        if (manualCameraInput)
        {
            tps.lockOnManualOverrideTimer =
                tps.lockOnManualOverrideTime;
        }
        else
        {
            tps.lockOnManualOverrideTimer =
                std::max(
                    0.0f,
                    tps.lockOnManualOverrideTimer - dt);
        }

        // ===================================================================
        // 3-4. ロックオン開始を検出
        // ===================================================================

        const bool lockOnJustStarted =
            tps.isLockOnActive &&
            !tps.wasLockOnActive;

        if (lockOnJustStarted)
        {
            // ロックオン開始時だけピッチを
            // lockOnPitch へ滑らかに近づける。
            //
            // ロックオン中ずっと固定するわけではない。
            const float pitchBlend =
                1.0f -
                std::exp(-tps.lockOnPitchDamping * dt);

            tps.currentPitch =
                std::lerp(
                    tps.currentPitch,
                    tps.lockOnPitch,
                    pitchBlend);
        }

        // ===================================================================
        // 3-5. ロックオン中のYaw自動アシスト
        // ===================================================================

        if (enemyTrans &&
            tps.lockOnManualOverrideTimer <= 0.0f)
        {
            const Vector3 playerPos =
                targetTrans->GetRenderWorldPosition();

            const Vector3 enemyPos =
                enemyTrans->GetRenderWorldPosition();

            const Vector3 playerToEnemy =
                enemyPos - playerPos;

            // ---------------------------------------------------------------
            // 敵の方向をYaw角に変換
            // ---------------------------------------------------------------

            const float idealYawRad =
                atan2f(
                    playerToEnemy.z,
                    playerToEnemy.x);

            const float idealYawDeg =
                XMConvertToDegrees(idealYawRad);

            // ---------------------------------------------------------------
            // 現在のカメラYawとの差
            // ---------------------------------------------------------------

            float yawDiff =
                idealYawDeg -
                tps.currentYaw;

            // -180 ～ +180 に正規化
            while (yawDiff > 180.0f)
                yawDiff -= 360.0f;

            while (yawDiff < -180.0f)
                yawDiff += 360.0f;

            // ---------------------------------------------------------------
            // Dead Zone
            //
            // 敵が少しずれているだけならカメラを動かさない。
            // ---------------------------------------------------------------

            if (std::abs(yawDiff) >
                tps.lockOnYawDeadZone)
            {
                // Dead Zoneを超えた分だけ補正する。
                const float excess =
                    std::abs(yawDiff) -
                    tps.lockOnYawDeadZone;

                // 超えた量に応じて補正速度を増やす。
                float correctionSpeed =
                    excess *
                    tps.lockOnAssistStrength;

                // 最大速度を制限。
                correctionSpeed =
                    std::min(
                        correctionSpeed,
                        tps.lockOnMaxCorrectionSpeed);

                // 度/秒 → 今フレームの角度へ。
                float correction =
                    correctionSpeed * dt;

                // 敵が左なら左へ、
                // 敵が右なら右へ。
                if (yawDiff < 0.0f)
                    correction = -correction;

                tps.currentYaw += correction;
            }
        }

        // ===================================================================
        // 3-6. Pitch制限
        // ===================================================================

        tps.currentPitch =
            std::clamp(
                tps.currentPitch,
                tps.minPitch,
                tps.maxPitch);

        // ===================================================================
        // 3-7. ロックオン状態を保存
        // ===================================================================

        tps.wasLockOnActive =
            tps.isLockOnActive;

        

        // ヨーは周回するたび無限に積算されるので ±360 に畳む。
        //   放置すると float の有効桁が回転量に食われ、長時間プレイで視点が
        //   カクつくようになる（プレハブに -4572 度が保存されていた）。
        //   ロックオン側は yawDiff で最短回転を取っているため、この畳み込みで
        //   角度が飛んでも挙動は変わらない。
        tps.currentYaw = std::fmod(tps.currentYaw, 360.0f);


        // 回り込み中心(pivot)と距離。通常は注視点中心、ロックオン中はプレイヤー中心。
        // 寄り要求中（切り払い準備など）は基準距離を closeUpDistance へ寄せる。
        //   実際の寄り/戻りは currentCamDistance の指数減衰で滑らかに補間される。
        const float baseDistance = tps.closeUpRequest ? tps.closeUpDistance : tps.distance;
        Vector3 posPivotTarget = idealLookAt;
        float   camDistance    = baseDistance;

        if (enemyTrans)
        {
            // TPSロックオンのフレーミング（遠距離でプレイヤーが画面外に出る問題の対策）:
            //   (1) 注視点はプレイヤー→敵へ寄せるが、ずれ量に上限(lockOnMaxLookShift)を設け、
            //       遠距離でもプレイヤーを必ずフレーム内に残す。
            //   (2) 回り込み中心はプレイヤー基準にする。従来は注視点(中間地点)を中心に
            //       引いていたため、敵が遠いとカメラがプレイヤーより前に出て画面外になっていた。
            //   (3) 敵が離れるほどカメラを引き、プレイヤーと敵の両方をフレームに収める。
            Vector3 pivot     = targetTrans->GetRenderWorldPosition() + tps.targetOffset;
            Vector3 enemyCore = enemyTrans->GetRenderWorldPosition() + tps.targetOffset;

            Vector3 lookShift = (enemyCore - pivot) * tps.lockOnSubjugationRatio;
            float   shiftLen  = lookShift.Length();
            if (shiftLen > tps.lockOnMaxLookShift && shiftLen > 1e-4f)
                lookShift *= (tps.lockOnMaxLookShift / shiftLen); // プレイヤーを画面内に保つクランプ
            idealLookAt = pivot + lookShift;

            const float sep = (enemyCore - pivot).Length();
            posPivotTarget  = pivot; // カメラはプレイヤーを中心に回り込む
            camDistance     = baseDistance +
                              std::clamp(sep * tps.lockOnDistanceGain, 0.0f, tps.lockOnMaxExtraDistance);
        }
        else
        {
            // 通常時のみルックアヘッド（進行方向への視点ズレ）を適用
            auto *targetInput = m_world->GetComponent<CharacterMovementInputComponent>(tps.target.id);
            if (targetInput)
            {
                Vector3 vel             = targetInput->currentVelocity;
                vel.y                   = 0.0f;
                Vector3 targetLookahead = vel * tps.lookaheadMultiplier;
                float   laBlend         = 1.0f - std::exp(-tps.lookaheadDamping * dt);
                tps.currentLookahead    = Vector3::Lerp(tps.currentLookahead, targetLookahead, laBlend);
                idealLookAt += tps.currentLookahead;
            }
            posPivotTarget = idealLookAt; // 通常時は従来どおり注視点中心に回り込む
        }

        // 初回フレーム初期化
        if (tps.isFirstFrame)
        {
            tps.currentLookAt      = idealLookAt;
            tps.currentPivot       = posPivotTarget;
            tps.currentCamDistance = camDistance;
            vcam.fov               = tps.baseFov;
            tps.isFirstFrame       = false;
        }

        // 5. 指数減衰による追従（注視点と回り込み中心をそれぞれ滑らかに追う）
        float posBlend    = 1.0f - std::exp(-tps.positionDamping * dt);
        tps.currentLookAt = Vector3::Lerp(tps.currentLookAt, idealLookAt, posBlend);
        tps.currentPivot  = Vector3::Lerp(tps.currentPivot, posPivotTarget, posBlend);

        // 6. 球面座標から配置位置の算出
        //    【符号の規約】pitch が正 → backwardDir.y が正 → finalPos は pivot より「下」。
        //    つまり maxPitch はカメラが真下に回り込んで見上げる限界、
        //          minPitch は上から見下ろす限界を表す。
        float pitchRad = XMConvertToRadians(tps.currentPitch);
        float yawRad   = XMConvertToRadians(tps.currentYaw);

        Vector3 backwardDir;
        backwardDir.x = cosf(yawRad) * cosf(pitchRad);
        backwardDir.y = sinf(pitchRad);
        backwardDir.z = sinf(yawRad) * cosf(pitchRad);
        backwardDir.Normalize();

        // =====================================================================
        // 6.5 カメラ衝突（スプリングアームのコリジョン）
        //   回り込み中心(pivot)からカメラ本体へ向けて地形(NON_MOVING)をレイキャストし、
        //   壁/建物に当たったらカメラをその手前へ寄せる（＝モデルへめり込ませない）。
        //   ・寄せる(内側)のは速く、戻す(外側)のはゆっくり＝標準的なスプリングアーム挙動。
        //   ・当たったのが地形だけなので、間をドローンやボスが横切ってもカメラは飛び込まない。
        // =====================================================================
        float desiredDist  = camDistance;
        float resolvedDist = desiredDist;
        if (tps.collisionEnabled && physicsAPI)
        {
            Vector3        castDir = -backwardDir; // pivot からカメラ本体へ向かう向き
            const float    maxDist = desiredDist + tps.collisionRadius;
            XMFLOAT3       o       = {tps.currentPivot.x, tps.currentPivot.y, tps.currentPivot.z};
            XMFLOAT3       d       = {castDir.x, castDir.y, castDir.z};
            PhysicsHitInfo hit     = physicsAPI->RayCastStatic(o, d, maxDist);
            if (hit.hit)
            {
                // 地形（壁も床も）に当たったら、その少し手前（collisionRadius ぶん）に置いてめり込みを防ぐ。
                resolvedDist = hit.distance - tps.collisionRadius;
                resolvedDist = std::clamp(resolvedDist, tps.collisionMinDistance, desiredDist);
            }
        }

        // fast-in / slow-out：内側へ寄せるのは素早く、外側へ戻すのはゆっくり補間する。
        float colDamping = (resolvedDist < tps.currentCamDistance) ? tps.collisionInDamping : tps.collisionOutDamping;
        float colBlend   = 1.0f - std::exp(-colDamping * dt);
        tps.currentCamDistance = std::lerp(tps.currentCamDistance, resolvedDist, colBlend);

        // 回り込み中心から後方へ（衝突を反映した）距離ぶん引いた位置がカメラ本体。
        Vector3 finalPos = tps.currentPivot - (backwardDir * tps.currentCamDistance);

        // カメラが寄った時は「足元」ではなく「頭付近」へ寄せる。
        //   引き込み量(pullRatio)に応じて、カメラのY(高さ)を pivot(頭)の高さへ持ち上げる。
        //   これで壁/床でカメラが近づいても視点が足元に落ちず、頭〜肩の高さを保つ。
        float range     = (std::max)(0.001f, desiredDist - tps.collisionMinDistance);
        float pullRatio = std::clamp((desiredDist - tps.currentCamDistance) / range, 0.0f, 1.0f);
        float liftAmount = pullRatio * tps.collisionHeadBias;
        finalPos.y      = std::lerp(finalPos.y, tps.currentPivot.y, liftAmount);

        // 持ち上げた先は上のレイキャストの経路上に無い（Y方向へずらしたため）。
        //   庇や階段裏では、寄せたはずのカメラが持ち上げによって再びジオメトリの
        //   内側へ入ってしまう。持ち上げが実際に効いた時だけ、確定位置へ向けて
        //   もう一度だけ地形を確認して押し戻す。
        if (tps.collisionEnabled && physicsAPI && liftAmount > 0.001f)
        {
            Vector3     toCam    = finalPos - tps.currentPivot;
            const float liftDist = toCam.Length();
            if (liftDist > 1e-4f)
            {
                Vector3 liftDir = toCam / liftDist;
                XMFLOAT3 o      = {tps.currentPivot.x, tps.currentPivot.y, tps.currentPivot.z};
                XMFLOAT3 d      = {liftDir.x, liftDir.y, liftDir.z};
                PhysicsHitInfo hit = physicsAPI->RayCastStatic(o, d, liftDist + tps.collisionRadius);
                if (hit.hit)
                {
                    float safeDist = std::clamp(hit.distance - tps.collisionRadius,
                                                tps.collisionMinDistance, liftDist);
                    finalPos = tps.currentPivot + liftDir * safeDist;
                }
            }
        }

        // 7. 動的FOV
        // 「画角を広げたい」という要求はゲーム側(PlayerDashSystem 等)が
        // wideFovRequest に書き込む。カメラは理由を知らずに要求だけを見る。
        float targetFov = tps.wideFovRequest ? tps.dashFov : tps.baseFov;
        float fovBlend = 1.0f - std::exp(-tps.fovDamping * dt);
        vcam.fov       = std::lerp(vcam.fov, targetFov, fovBlend);

        // 8. TransformとVirtualCameraバッファの更新
        trans.position = finalPos;

        XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), backwardDir, Vector3::Up);
        XMVECTOR det;
        XMMATRIX worldRot = XMMatrixInverse(&det, lookMat);
        trans.rotation    = Quaternion::CreateFromRotationMatrix(worldRot);

        vcam.resultPos    = finalPos;
        vcam.resultLookAt = tps.currentLookAt;
        vcam.resultUp     = Vector3::Up;
        // 位置・注視点とも自前で確定させたので Brain が採用してよい
        vcam.MarkResolved();
    });

    // =====================================================================
    // カーソルロックの最終決定（フレームに1回）
    //   マウスルック中でも、以下では固定しない（＝解除する）:
    //     ・ポーズ中（メニュー操作にカーソルが要る）
    //     ・エディタで他ウィンドウ操作中（IsGameInputActive=false）
    //   さらに Mouse 側で「非フォーカス時は自動解除」しているため、
    //   alt-tab でカーソルが囚われることもない。
    // =====================================================================
    if (cursorFreeOverride)
        wantCursorLock = false;

    inputAPI->SetMouseLock(wantCursorLock);
}


// 描画ステージ（Render）ではなく、ロジック確定直後のL02フェーズでカメラの姿勢を確定させる
REGISTER_RENDER_SYSTEM(CameraActionTPSSystem, Priority::RenderStage::R01_Camera);
