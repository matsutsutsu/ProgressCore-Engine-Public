#pragma once
// ===========================================================================
// 概要: トレイルの時間をロジック（60Hz 固定）で進め、描画側へ「1区間刻んでよい」印を
//       出す System（L06_Resolution）。
//
// 責務:
//   - 断面の age を固定 dt で進め、lifeTime を超えたものを古い順に捨てる
//   - burstTimer を進め、切れたら emitting を落とす
//   - emitting なら sampleRequested を立てる
//
// 備考: 実際に断面を刻んで形を作るのは TrailSystem（描画側）。
//       emitting を決める TrailAnimationSyncSystem（L05）と、ボーン追従（L03）・物理（L04/L05）より後に走る。
//
//       なぜ刻む頻度をロジックに固定するか: アニメーションの再生時刻はロジック（60Hz）でしか
//       進まないので、剣の振り（ボーン姿勢）も 60Hz でしか変わらない。描画のレートで刻むと、
//       90Hz/144Hz では姿勢が前のフレームと同じフレームができ、プレイヤーの補間移動の分だけ
//       ずれた断面が重なって、尾が二重の線に見える。頻度をロジックに固定すれば、
//       リフレッシュレートによらず同じ尾になる。
//       ただし刻む位置は描画側の補間済み行列のまま。ロジックの行列を使うと尾が剣から離れる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/Trail/TrailComponent.h"

class TrailRecordSystem
    : public CCL::ECS::IfSystem<TrailRecordSystem,
                                CCL::ECS::Write<TrailComponent>>
{
public:
    TrailRecordSystem() : IfSystem("TrailRecordSystem") {}

    void Update(float dt) override;
};
