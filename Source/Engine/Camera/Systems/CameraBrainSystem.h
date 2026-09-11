#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include <DirectXMath.h>
#include <vector>

// 前方宣言 (インクルード削減のため)
class Camera;

class CameraBrainSystem
    : public CCL::ECS::IfSystem<CameraBrainSystem, CCL::ECS::Read<VirtualCamera>> {
  private:
    // ブレンド管理用
    CCL::ECS::EntityID m_currentCamID = CCL::ECS::InvalidEntityID; // 現在のメイン
    CCL::ECS::EntityID m_prevCamID    = CCL::ECS::InvalidEntityID; // 遷移元

    float m_blendDuration = 1.5f; // デフォルトブレンド時間
    float m_blendTimer    = 0.0f;

    // 前回のカメラ状態（遷移元が消滅した場合のバックアップ用）
    DirectX::XMFLOAT3 m_lastPos  = {0, 0, -10};
    DirectX::XMFLOAT3 m_lastLook = {0, 0, 0};
    DirectX::XMFLOAT3 m_lastUp   = {0, 1, 0};
    float             m_lastFov  = 45.0f;
    // Near/Farのバックアップ
    float m_lastNear = 0.1f;
    float m_lastFar  = 1000.0f;

  public:
    CameraBrainSystem() : IfSystem("CameraBrainSystem") {}

    void Update(float dt) override;

  private:
    /**
     * @brief 採用可能な vCam のうち、最も優先度の高いものを選ぶ。
     * @param outID   選ばれた vCam のエンティティID（見つからなければ InvalidEntityID）
     * @return        選ばれた vCam。見つからなければ nullptr。
     */
    const VirtualCamera *SelectHighestPriorityCamera(CCL::ECS::EntityID &outID);
};
