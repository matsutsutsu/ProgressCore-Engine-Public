#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include <DirectXMath.h>
#include <vector>

// 前方宣言 (インクルード削減のため)
class Camera;

class CameraBrainSystem : public CCL::ECS::SystemBase {
  private:
    // ブレンド管理用
    CCL::ECS::EntityID m_currentCamID = CCL::ECS::InvalidEntityID; // 現在のメイン
    CCL::ECS::EntityID m_prevCamID    = CCL::ECS::InvalidEntityID; // 遷移元

    float m_blendDuration = 1.5f; // デフォルトブレンド時間
    float m_blendTimer    = 0.0f;

    // 前回のカメラ状態（遷移元が消滅した場合のバックアップ用）
    DirectX::XMFLOAT3 m_lastPos  = {0, 0, -10};
    DirectX::XMFLOAT3 m_lastLook = {0, 0, 0};
    float             m_lastFov  = 45.0f;
    // Near/Farのバックアップ
    float m_lastNear = 0.1f;
    float m_lastFar  = 1000.0f;

  public:
    CameraBrainSystem();
    virtual ~CameraBrainSystem() = default;

    // どのデータにアクセスするかをスケジューラに自己申告する
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    void Update(float dt) override;
};