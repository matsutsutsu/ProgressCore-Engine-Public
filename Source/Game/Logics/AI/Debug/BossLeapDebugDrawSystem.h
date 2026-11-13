#pragma once
// ===========================================================================
// 概要: ボスの跳躍の「予定の軌道」と「実際に通った軌道」を、ステージ上に3Dで描くデバッグ用の System（エディタのみ）
//
// 責務:
//   - 踏み切り地点・着地点（攻撃範囲の円）・予定の軌道・実際に通った軌道を描く
//   - 着地の扱いになった瞬間の位置に印を付け、予定の着地点からの高さのずれを出す
//   - 水平距離・頂点の高さ・飛ぶ時間を、予定と実際で並べて調整 UI に出す
//
// 備考: 「空中で着地する」「着地がゆっくりすぎる」などを、跳躍の調整中に目で確かめるためのもの
//       読むだけで、ボスの動きには触らない
//       予定の軌道は、予約した移動（CharacterMovementComponent）を段ごとに拾い、
//       物理と同じ式（ReservationPlannedPosition）で描く。式を2か所で持つと、線と実際の動きが食い違う
//       跳躍が終わっても、次の跳躍が始まるまで最後の1回を描き続ける
//       1対1の戦闘前提で、最初に見つかったボスだけを見る
// ===========================================================================
#include <cstdint>
#include <vector>

#include <DirectXMath.h>
#include <SimpleMath.h>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

class ShapeRenderer;

class BossLeapDebugDrawSystem : public CCL::ECS::IfSystem<
    BossLeapDebugDrawSystem,
    CCL::ECS::Read<BossActionComponent>,
    CCL::ECS::Read<CharacterMovementComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>>
{
public:
    BossLeapDebugDrawSystem() : IfSystem("BossLeapDebugDrawSystem") {}

    void Update(float dt) override; // 跳躍を記録し、ShapeRenderer へ積む
    void OnGui() override;          // 表示の切り替えと、最後の跳躍の数値

private:
    // 1回の跳躍の記録
    struct LeapRecord
    {
        bool     valid        = false; // 記録があるか
        uint32_t actionSerial = 0;     // どの依頼の跳躍か（同じ技を続けて出したときに分ける）

        BossLeapDef                  leapDef;                                          // 跳び方（入った瞬間に写す）
        DirectX::SimpleMath::Vector3 takeoffPos  = DirectX::SimpleMath::Vector3::Zero; // 踏み切った地点
        DirectX::SimpleMath::Vector3 landingPos  = DirectX::SimpleMath::Vector3::Zero; // 予定の着地点
        bool                         tookOff     = false;                              // 踏み切ったか
        float                        takeoffTime = 0.0f;                               // 踏み切った時刻（技の経過 [秒]）

        std::vector<CharacterMoveReservation>     segments; // 予約した移動の段（踏み切りから着地まで）
        std::vector<DirectX::SimpleMath::Vector3> trail;    // 実際に通った位置（足元）

        bool                         landed           = false;                              // 着地の扱いになったか
        DirectX::SimpleMath::Vector3 landedPos        = DirectX::SimpleMath::Vector3::Zero; // そのときの位置
        float                        landedTime       = 0.0f;                               // そのときの時刻（技の経過 [秒]）
        size_t                       landedTrailIndex = 0;                                  // trail のどこから着地の後か
    };

    // このフレームのボスの様子を記録に足す。跳躍に入った瞬間は記録を作り直す
    void Record(const BossActionComponent &bossAction, const CharacterMovementComponent &movement,
                const DirectX::SimpleMath::Vector3 &bossPos);

    // 記録を描く
    void Draw(ShapeRenderer *renderer) const;

    // 予約した移動の段を1つ描く（MoveTo は線、Hold は球）
    void DrawPlannedSegment(ShapeRenderer *renderer, const CharacterMoveReservation &segment) const;

    // 予定の軌道で一番高い所の、踏み切りからの高さ [m]
    float PlannedApexHeight() const;

    // 実際に通った軌道で一番高い所の、踏み切りからの高さ [m]
    float ActualApexHeight() const;

    bool       m_enabled       = false; // 描くか
    float      m_landingRadius = 8.0f;  // 着地点に描く円の半径 [m]（Leap_End_Attack の攻撃判定の半径）
    bool       m_wasLeaping    = false; // 前のフレームに跳躍していたか
    LeapRecord m_record;                // 最後の跳躍
};
