#pragma once
/// @file CameraTargetBindSystem.h
/// @brief カメラの追従/注視ターゲットを「役割(PlayerComponent)」で解決して繋ぎ直すシステム。
///
/// なぜゲーム側にあるか:
///   「どのエンティティがプレイヤーか」はゲームの知識であり、Engine のカメラが
///   PlayerComponent を知っていると Engine→Game 依存になる。そこで解決役を
///   ゲーム側に置き、Engine のカメラコンポーネント(target)へ push する。
///
/// なぜ必要か:
///   ハードコードした EntityID ではなく役割で繋ぐため、プレハブ/レベルで
///   プレイヤーとカメラを別々に配置しても、save/load やリスポーンで ID が
///   変わっても、実行時に自動でカメラ target をプレイヤーへ繋ぎ直せる。
///
/// 実行ステージ:
///   R00b_CameraTargetBind(75)。カメラ本体(R01_Camera=100)より確実に前へ置く。
///   移管前も同じ「カメラ更新の直前」に解決していたため、タイミングは不変。
///   ※ Logic ステージに置くと FixedUpdate が走らないフレームで解決が遅れるため、
///     あえて Render ステージのカメラ直前に置いている。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"

class CameraTargetBindSystem
    : public CCL::ECS::IfSystem<CameraTargetBindSystem,
          CCL::ECS::Read<VirtualCamera>>
{
  public:
    CameraTargetBindSystem()
        : IfSystem("CameraTargetBindSystem")
    {
    }
    virtual ~CameraTargetBindSystem() = default;

    void Update(float dt) override;
};
