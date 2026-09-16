#pragma once
// ===========================================================================
// TrailRecordSystem.h
// -------------------
// 役割:
//   トレイルの「時間」をロジック相(60Hz固定)で進め、描画相へ「1区間刻んでよい」
//   という印を発行する。実際の区間の刻みと形状生成は TrailSystem(描画相)が行う。
//
// 責務:
//   - Segment.age を固定 dt で進め、lifeTime を超えた区間を最古側から破棄する。
//   - burstTimer を進め、切れたら emitting を落とす。
//   - emitting なら sampleRequested を立てる（刻む頻度をロジック相へ固定する）。
//
// 位置付け:
//   L06_Resolution。emitting を駆動する TrailAnimationSyncSystem(L05_Collision)、
//   ボーン追従(L03_PrePhysics)、物理(L04/L05)のいずれよりも後。
//
// 実装の意図(Why):
//   剣の振り＝ボーンの globalTransform は AnimationSystem(L02_PostUpdate)が更新するため
//   60Hz でしか進まない。以前はこの老化と刻みが全て描画相にあったので、90Hz/144Hz の
//   環境では「ボーン姿勢が前フレームと同一のフレーム」が生まれ、そこでプレイヤーの
//   補間移動ぶんだけずれた断面が重複して刻まれていた（尾が二重の線に見える原因）。
//   刻む頻度をロジック相に固定すれば、リフレッシュレートに依存しない同一の尾になる。
//   ※サンプル位置自体は描画相の補間済み行列のまま。こうしないと尾が剣から離れる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/Trail/TrailComponent.h"

// トレイルの老化と「刻んでよい」印の発行を担うロジックシステム。
class TrailRecordSystem
    : public CCL::ECS::IfSystem<TrailRecordSystem,
                                CCL::ECS::Write<TrailComponent>>
{
public:
    TrailRecordSystem() : IfSystem("TrailRecordSystem") {}

    void Update(float dt) override;
};
