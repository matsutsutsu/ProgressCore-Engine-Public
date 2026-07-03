/// @file CameraActionTPSSystem.cpp
/// @brief CameraActionTPSSystem のUpdateロジック実装。

#include "Engine/Camera/Systems/CameraActionTPSSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Combat/DeadTag.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

void CameraActionTPSSystem::Initialize()
{
    // JUST回避の衝撃をカメラに伝達する
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        // 自分の担当している仮想カメラを走査
        ForEach([&](VirtualCamera &vcam, const CameraBodyTPSComponent &tps, TransformComponent &trans) {
            // JUST回避したプレイヤーを映しているカメラなら、衝撃(FOVパンチ)を与える
            if (tps.target.id == ev.playerID)
            {
                // 画角を瞬間的に限界まで広げる。
                // 次のフレームから自動的に「fovDamping」の摩擦力によって滑らかに元に戻る。
                vcam.fov = 130.0f;
            }
        });
    });

  // 着地などの汎用カメラシェイクを受信
    ListenEvent<CameraShakeRequestEvent>([this](const CameraShakeRequestEvent &ev) {

        ForEachWithID([&](EntityID id, VirtualCamera &vcam, CameraBodyTPSComponent &tps, TransformComponent &trans) {
            if (!_world->HasComponent<CameraShake>(id))
            {
                _world->AddComponent<CameraShake>(id);
            }
            if (auto *shake = _world->GetComponent<CameraShake>(id))
            {
                shake->SetShake(ev.amplitude, ev.duration);
            }
        });
    });

}

void CameraActionTPSSystem::Update(float dt)
{
    if (!_world->HasResource<std::shared_ptr<IInputAPI>>())
        return;
    auto inputAPI = _world->GetResource<std::shared_ptr<IInputAPI>>();

    float lookX = inputAPI->GetAxis("LookX");
    float lookY = inputAPI->GetAxis("LookY");

    // ForEachは IfSystem のテンプレート引数(3つのコンポーネント)と完全に一致させる
    ForEach([&](VirtualCamera &vcam, CameraBodyTPSComponent &tps, TransformComponent &trans) {
        // 1. 追従対象（プレイヤー）の取得
        auto *targetTrans = _world->GetComponent<TransformComponent>(tps.target.id);
        if (!targetTrans) return;

        // ===================================================================
        // ★データブリッジ：プレイヤーコンポーネントからロックオン情報を自動同期する
        // これにより、カメラがプレイヤー固有のロックオン状態を完全に追従する
        // ===================================================================
        if (auto *playerComp = _world->GetComponent<PlayerComponent>(tps.target.id))
        {
            tps.lockOnTarget.id = playerComp->lockOnTargetID;
            tps.isLockOnActive  = (playerComp->lockOnTargetID != CCL::ECS::InvalidEntityID);
        }

        // 2. ロックオン対象（エネミー）の生存確認とTransform取得
        auto *enemyTrans = tps.isLockOnActive ? _world->GetComponent<TransformComponent>(tps.lockOnTarget.id) : nullptr;

        // 敵が死亡タグを持っていたらロックオンを自動終了させる安全ハック
        if (tps.isLockOnActive && (!enemyTrans || _world->HasComponent<Tag::DeadTag>(tps.lockOnTarget.id)))
        {
            tps.isLockOnActive = false;
            enemyTrans         = nullptr;
        }

        // 3. オービット（回転角）の決定
        // 3. オービット（回転角）の決定
        if (enemyTrans)
        {
            // ===============================================================
            // 【ロックオンモード】: 球面座標系に完全準拠した角度の自動逆算
            // ===============================================================
            // backwardDir は実質的に「カメラのForwardベクトル」として機能しているため、
            // 「プレイヤーから敵へ向かうベクトル」を算出して向きを合わせる
            Vector3 playerToEnemy = enemyTrans->position - targetTrans->position;

            // X軸=cos, Z軸=sin の球面座標系に完全に噛み合わせる
            float idealYawRad = atan2f(playerToEnemy.z, playerToEnemy.x);
            float idealYawDeg = XMConvertToDegrees(idealYawRad);

            // 最短回転（角度の180度跨ぎでカメラが逆回転するのを防ぐハック）
            float yawDiff = idealYawDeg - tps.currentYaw;
            while (yawDiff > 180.0f)
                yawDiff -= 360.0f;
            while (yawDiff < -180.0f)
                yawDiff += 360.0f;

            // 滑らかな自動回り込み補間
            float rotBlend = 1.0f - std::exp(-tps.lockOnRotationDamping * dt);
            tps.currentYaw += yawDiff * rotBlend;

            // ピッチ角はエディタの設定値へ自動追従
            tps.currentPitch = std::lerp(tps.currentPitch, tps.lockOnPitch, rotBlend);
        }
        else
        {
            // ===============================================================
            // 【通常モード】: スティック入力による自由カメラ操作
            // ===============================================================
            tps.currentYaw += lookX * tps.lookSpeedX * dt;
            tps.currentPitch += lookY * tps.lookSpeedY * dt;
            tps.currentPitch = std::clamp(tps.currentPitch, tps.minPitch, tps.maxPitch);
        }

        // 4. 注視点（LookAt）の算出
        Vector3 idealLookAt = targetTrans->position + tps.targetOffset;

        if (enemyTrans)
        {
            // ★ AAAハック: 自分と敵の中間地点をブレンド計算し、両方を画面に収める
            Vector3 enemyCorePos = enemyTrans->position + tps.targetOffset; // 敵もプレイヤーと同じ高さのコアを狙う
            idealLookAt          = Vector3::Lerp(idealLookAt, enemyCorePos, tps.lockOnSubjugationRatio);
        }
        else
        {
            // 通常時のみルックアヘッド（進行方向への視点ズレ）を適用
            auto *targetInput = _world->GetComponent<CharacterMovementInputComponent>(tps.target.id);
            if (targetInput)
            {
                Vector3 vel             = targetInput->currentVelocity;
                vel.y                   = 0.0f;
                Vector3 targetLookahead = vel * tps.lookaheadMultiplier;
                float   laBlend         = 1.0f - std::exp(-tps.lookaheadDamping * dt);
                tps.currentLookahead    = Vector3::Lerp(tps.currentLookahead, targetLookahead, laBlend);
                idealLookAt += tps.currentLookahead;
            }
        }

        // 初回フレーム初期化
        if (tps.isFirstFrame)
        {
            tps.currentLookAt = idealLookAt;
            vcam.fov          = tps.baseFov;
            tps.isFirstFrame  = false;
        }

        // 5. 指数減衰による追従
        float posBlend    = 1.0f - std::exp(-tps.positionDamping * dt);
        tps.currentLookAt = Vector3::Lerp(tps.currentLookAt, idealLookAt, posBlend);

        // 6. 球面座標から配置位置の算出
        float pitchRad = XMConvertToRadians(tps.currentPitch);
        float yawRad   = XMConvertToRadians(tps.currentYaw);

        Vector3 backwardDir;
        backwardDir.x = cosf(yawRad) * cosf(pitchRad);
        backwardDir.y = sinf(pitchRad);
        backwardDir.z = sinf(yawRad) * cosf(pitchRad);
        backwardDir.Normalize();

        Vector3 finalPos = tps.currentLookAt - (backwardDir * tps.distance);

        // 7. 動的FOV
        float targetFov = tps.baseFov;
        auto *dash      = _world->GetComponent<DashComponent>(tps.target.id);
        if (dash && dash->isDashing)
        {
            targetFov = tps.dashFov;
        }
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
        vcam.isValid      = true;
    });
}


// 描画ステージ（Render）ではなく、ロジック確定直後のL02フェーズでカメラの姿勢を確定させる
REGISTER_LOGIC_SYSTEM(CameraActionTPSSystem, Priority::LogicStage::L02_Update);