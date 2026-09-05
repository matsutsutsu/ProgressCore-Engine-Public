// 敵AIの意思決定を戦場そのものへ3D描画するデバッグシステム。
// 地面の距離リングでBTの分岐しきい値（近接<8m / 接近>40m）を見せ、「なぜ今この枝を
// 選んだか」を戦いながら読めるようにする。あわせてボス→プレイヤーの照準線を引く。
//
// AIDebugOverlaySystem（頭上の ImGui テキスト）とは描画機構もタイミングも違うので、
// 専用のレンダーシステムとして独立させてある。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

class AIDebugSpatialSystem : public CCL::ECS::IfSystem<
                                AIDebugSpatialSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Read<BossActionComponent>>
{
  public:
    AIDebugSpatialSystem() : IfSystem("AIDebugSpatialSystem") {}

    /// ShapeRenderer への描画発行（毎フレーム）。
    void Update(float dt) override;
    void OnGui() override;

  private:
    bool  m_enabled  = true;
    bool  m_drawLine = true;   // ボス→プレイヤーの照準線d
    //bool  m_enabled  = false;
    //bool  m_drawLine = false;  
    float m_ringNear = 8.0f;   // BTConditions::RangeMostClose (<8m) と対応。近接の的
    float m_ringFar  = 40.0f;  // BTConditions::RangeFar (>40m) と対応。接近(逃げ得)の境界
};
