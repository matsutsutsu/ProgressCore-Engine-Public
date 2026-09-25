#pragma once
// ===========================================================================
// 概要: CameraBodyTPSComponent を持つカメラの位置・向き・画角を毎フレーム計算する System。
//
// 責務:
//   - 右スティック／マウス入力でオービット角を更新する
//   - ロックオン中のフレーミングとヨーの自動補正
//   - 地形へのめり込み回避（レイキャスト）とルックアヘッド
//   - FOV パンチ要求（CameraFovPunchRequestEvent）の受信と、マウスルック中のカーソルロック
//
// 備考: 画面揺れはここでは扱わない（CameraShakeSystem が後段で加算する）。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class CameraActionTPSSystem : public CCL::ECS::IfSystem<
                                  CameraActionTPSSystem,
                                  CCL::ECS::Write<VirtualCamera>,
                                  CCL::ECS::Write<CameraBodyTPSComponent>, // 内部状態(Pitch/Yaw等)を更新するためWrite
                                  CCL::ECS::Write<TransformComponent>>     // カメラ自身のTransformを確定させるためWrite
{
public:
    CameraActionTPSSystem()
        : IfSystem("CameraActionTPSSystem")
    {
    }
    virtual ~CameraActionTPSSystem() = default;

    void Initialize() override;
    void Update(float dt) override;
};
