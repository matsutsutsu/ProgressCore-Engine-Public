#pragma once
// ===========================================================================
// 概要: 採用可能な vCam から最も優先度の高いものを選び、メインカメラ（Camera リソース）
//       へ反映する System。
//
// 責務:
//   - 最高優先度の vCam の選択（同値は EntityID で決定的に決める）
//   - 切り替え時に、新しい vCam の blendTime で位置・注視点・画角・クリップ面を補間する
//   - 遷移元が消えたときのために、前フレームの結果を保持する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include <DirectXMath.h>

class Camera;

class CameraBrainSystem
    : public CCL::ECS::IfSystem<CameraBrainSystem, CCL::ECS::Read<VirtualCamera>>
{
private:
    CCL::ECS::EntityID m_currentCamID = CCL::ECS::InvalidEntityID; // 現在採用している vCam
    CCL::ECS::EntityID m_prevCamID    = CCL::ECS::InvalidEntityID; // ブレンドの遷移元

    float m_blendDuration = 1.5f; // 現在のブレンド時間（秒）。切り替え時に新しい vCam の blendTime で上書きする
    float m_blendTimer    = 0.0f; // ブレンド開始からの経過時間（秒）

    // 前フレームにメインカメラへ反映した値。遷移元が消えた・無効化されたときの補間元に使う
    DirectX::XMFLOAT3 m_lastPos  = {0, 0, -10};
    DirectX::XMFLOAT3 m_lastLook = {0, 0, 0};
    DirectX::XMFLOAT3 m_lastUp   = {0, 1, 0};
    float             m_lastFov  = 45.0f;
    float             m_lastNear = 0.1f;
    float             m_lastFar  = 1000.0f;

public:
    CameraBrainSystem() : IfSystem("CameraBrainSystem") {}

    void Update(float dt) override;

private:
    /**
     * @brief 採用可能な vCam のうち、最も優先度の高いものを選ぶ。
     * @param outID 選ばれた vCam のエンティティID（見つからなければ InvalidEntityID）
     * @return 選ばれた vCam。見つからなければ nullptr。
     */
    const VirtualCamera *SelectHighestPriorityCamera(CCL::ECS::EntityID &outID);
};
