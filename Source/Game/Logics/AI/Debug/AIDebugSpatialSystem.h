/// @file  AIDebugSpatialSystem.h
/// @brief 敵AIの意思決定を「戦場そのもの」に3D描画するデバッグシステム。
///
/// 設計方針:
///   AIDebugOverlaySystem が頭上に screen-space の ImGui テキストを描くのに対し、
///   こちらは ShapeRenderer で world-space の図形（距離リング・ロックオン線）を描く。
///   両者は描画機構もタイミングも異なる（ShapeRenderer はレンダーパスで消費される）ため、
///   専用のレンダーシステムとして独立させる（NavMeshDebugDrawSystem と同じ流儀）。
///
/// 何を見せるか:
///   ・地面の距離リング … BTの分岐しきい値（近接<8m / 接近>40m）を可視化し、
///     「なぜ今この枝を選んだか」を戦いを見ながら理解できるようにする。
///   ・ロックオン線 … ボス→プレイヤーの照準線。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Boss/Data/BossActionComponent.h"

/// @brief ボスの間合いリングと照準線を ShapeRenderer で world 描画するデバッグ用レンダーシステム。
class AIDebugSpatialSystem : public CCL::ECS::IfSystem<
                                AIDebugSpatialSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Read<BossActionComponent>>
{
  public:
    AIDebugSpatialSystem() : IfSystem("AIDebugSpatialSystem") { hasGui = true; }

    // ShapeRenderer への描画発行（毎フレーム）。
    void Update(float dt) override;
    void OnGui() override;

  private:
    bool  m_enabled  = true;
    float m_ringNear = 8.0f;   ///< BTConditions::RangeMostClose (<8m) と対応。近接の的
    float m_ringFar  = 40.0f;  ///< BTConditions::RangeFar (>40m) と対応。接近(逃げ得)の境界
    bool  m_drawLine = true;   ///< ボス→プレイヤーの照準線
};
