#pragma once
// ===========================================================================
// KnockbackSystem.h
// -----------------
// 役割:
//   KnockbackComponent を持つエンティティの吹っ飛び運動を毎フレーム進行させる。
//   残り速度を Transform に反映し減衰させ、停止したら isActive=false に戻す。
//
// 責務:
//   - isActive な対象のみ velocity を localDt で Transform.position に加算(XZのみ)。
//   - velocity を時間減衰させ、threshold 未満で isActive=false にして停止させる。
//
// アーキテクチャ上の位置付け:
//   有効化は DamageResolutionSystem(L06)。本Systemは次フレーム以降の L02_Update で
//   通常移動と並ぶ外力レイヤーとして消化する。Transform更新後、JoltPushSystem(L03)が
//   敵のKinematicボディを追従させるため、判定位置も自動で正しく追従する。
//
// 設計方針(重要):
//   - KnockbackComponent は敵に常設し、付け外しはしない。稼働制御は isActive のみで行う
//     ため構造変更(アーキタイプ移動)が一切発生しない。停止対象は velocity 加算と減衰を
//     スキップするだけで、走査コストは軽微。
//   - 敵はECS駆動(Transform)＋Jolt Kinematic(判定のみ)。ノックバックもJoltに力を渡さず
//     Transformを直接動かすため、過去の「Joltに力を渡して敵が吹き飛ぶ」事故は起きない。
//   - 構造変更が無く各エンティティ独立処理のため ForEachParallel で並列化できる。
// ===========================================================================
#include "Engine/ECS/System/CCL_System.h"
#include "Game/Logics/Combat/KnockbackComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"

class KnockbackSystem : public CCL::ECS::IfSystem<KnockbackSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<KnockbackComponent>,
    CCL::ECS::Read<TimeStateComponent>> {
public:
    KnockbackSystem() : IfSystem("KnockbackSystem") {}
    void Update(float dt) override;
};