/// @file CameraActionTPSSystem.cpp
/// @brief CameraActionTPSSystem のUpdateロジック実装。

#include "Engine/Camera/Systems/CameraActionTPSSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <algorithm>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

void CameraActionTPSSystem::Update(float dt)
{
    if (!_world->HasResource<std::shared_ptr<IInputAPI>>())
        return;
    auto inputAPI = _world->GetResource<std::shared_ptr<IInputAPI>>();

    // デバイス入力の回収（右スティック/マウス移動量）
    float lookX = inputAPI->GetAxis("LookX");
    float lookY = inputAPI->GetAxis("LookY");

    ForEach([&](VirtualCamera &vcam, CameraBodyTPSComponent &tps, TransformComponent &trans) {
        // 1. 追従対象（プレイヤー）のTransformを取得
        auto *targetTrans = _world->GetComponent<TransformComponent>(tps.target.id);
        if (!targetTrans)
            return;

        // 2. 入力による回転（オービット）の更新
        tps.currentYaw += lookX * tps.lookSpeedX * dt;
        tps.currentPitch += lookY * tps.lookSpeedY * dt;
        tps.currentPitch = std::clamp(tps.currentPitch, tps.minPitch, tps.maxPitch);

        // 3. 理想の注視点(ワールド空間)の算出
        Vector3 idealLookAt = targetTrans->position + tps.targetOffset;

        // 初回フレームは補間をスキップして強制ターゲット同期
        if (tps.isFirstFrame)
        {
            tps.currentLookAt = idealLookAt;
            vcam.fov          = tps.baseFov;
            tps.isFirstFrame  = false;
        }

        // 4. フレームレート非依存の指数減衰（Exponential Decay）による遅延追従
        float posBlend    = 1.0f - std::exp(-tps.positionDamping * dt);
        tps.currentLookAt = Vector3::Lerp(tps.currentLookAt, idealLookAt, posBlend);

        // 5. 球面座標からカメラの配置方向ベクトルを逆算
        float pitchRad = XMConvertToRadians(tps.currentPitch);
        float yawRad   = XMConvertToRadians(tps.currentYaw);

        Vector3 backwardDir;
        backwardDir.x = cosf(yawRad) * cosf(pitchRad);
        backwardDir.y = sinf(pitchRad);
        backwardDir.z = sinf(yawRad) * cosf(pitchRad);
        backwardDir.Normalize();

        // 仮想注視点から距離分だけ後ろに引いた位置を最終座標とする
        Vector3 finalPos = tps.currentLookAt - (backwardDir * tps.distance);

        // 6. 動的FOVの適用（ダッシュ状態の検知）
        float targetFov = tps.baseFov;
        auto *dash      = _world->GetComponent<DashComponent>(tps.target.id);
        if (dash && dash->isDashing)
        {
            targetFov = tps.dashFov;
        }

        // FOVも指数減衰で滑らかに遷移
        float fovBlend = 1.0f - std::exp(-tps.fovDamping * dt);
        vcam.fov       = std::lerp(vcam.fov, targetFov, fovBlend);

        // 7. データメンバの更新と同期
        trans.position = finalPos;

        // 回転行列の作成（LookAtから逆算）
        XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), backwardDir, Vector3::Up);
        XMVECTOR det;
        XMMATRIX worldRot = XMMatrixInverse(&det, lookMat);
        trans.rotation    = Quaternion::CreateFromRotationMatrix(worldRot);

        // 仮想カメラバッファへの書き込み（CameraBrainへの最終伝達データ）
        vcam.resultPos    = finalPos;
        vcam.resultLookAt = tps.currentLookAt;
        vcam.resultUp     = Vector3::Up;
        vcam.isValid      = true;
    });
}

// 描画ステージ（Render）ではなく、ロジック確定直後のL02フェーズでカメラの姿勢を確定させる
REGISTER_LOGIC_SYSTEM(CameraActionTPSSystem, Priority::LogicStage::L02_Update);