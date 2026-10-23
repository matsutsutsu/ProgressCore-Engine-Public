#pragma once
// ===========================================================================
// 概要: ドローン1機ずつの「持ち場とのずれ」を、ステージ上に3Dで描くデバッグ用の System（エディタのみ）。
//
// 責務:
//   - 持ち場から離れている機を球で囲み、持ち場の小さな球と線でつなぐ（色は状態ごと）
//   - 状態ごとの機数・剛体になれていない機の数・最大のずれを数え、調整 UI に出す
//
// 備考:
//   陣形攻撃で一部の機が遅れる・ずれるときに、どの機がどの状態で離れているかを見るためのもの。
//   読むだけで、ドローンの動きには触らない。
//   1000機ぶん描くと読めないので、しきい値より離れている機だけを描く。
// ===========================================================================
#include "SimpleMath.h"

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

class ShapeRenderer;

class DroneDebugDrawSystem : public CCL::ECS::IfSystem<
                                 DroneDebugDrawSystem,
                                 CCL::ECS::Read<TransformComponent>,
                                 CCL::ECS::Read<DroneComponent>>
{
public:
    DroneDebugDrawSystem() : IfSystem("DroneDebugDrawSystem") {}

    void Update(float dt) override; // ShapeRenderer へ積み、機数を数える
    void OnGui() override;          // 表示の切り替えと、数えた結果

private:
    // 1フレームぶんの数えた結果
    struct Stats
    {
        int   byState[4]  = {}; // DroneState ごとの機数（添字は DroneState の値）
        int   unlocked    = 0;  // 剛体追従の突撃中で、まだ剛体になれていない機の数
        float maxDistance = 0;  // 持ち場からの最大のずれ [m]。離脱中は持ち場を離れるのが正しいので除く
    };

    // 持ち場から離れている機を1機描く
    void DrawOffSlot(ShapeRenderer *renderer, const DirectX::SimpleMath::Vector3 &position,
                     const DirectX::SimpleMath::Vector3 &slot, const DirectX::XMFLOAT4 &color) const;

    bool  m_enabled     = false; // 描くか
    float m_minDistance = 0.5f;  // 持ち場からこれより離れている機だけ描く [m]
    Stats m_stats;               // 最後に描いたフレームの結果
};
