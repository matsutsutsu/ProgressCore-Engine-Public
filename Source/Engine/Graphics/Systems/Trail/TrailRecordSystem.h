#pragma once
// ===========================================================================
// 概要: トレイルの時間をロジック（60Hz 固定）で進め、描画側へ「1区間刻んでよい」印を
//       出す System（L06_Resolution）。
//
// 責務:
//   - 断面の age を進め、lifeTime を超えたものを古い順に捨てる
//     （animControlled なら持ち主の時計、それ以外はトレイル自身の時計）
//   - burstTimer を持ち主（ownerEntity）の時計で進め、切れたら emitting を落とす
//   - 老化に使った時間の倍率を timeScale に書く（描画側の UV スクロール用）
//   - emitting なら sampleRequested を立てる
//
// 備考: 実際に断面を刻んで形を作るのは TrailSystem（描画側）。
//       emitting と ownerEntity を決める TrailAnimationSyncSystem（L05）と、ボーン追従（L03）・物理（L04/L05）より後に走る。
//
//       なぜ刻む頻度をロジックに固定するか: アニメーションの再生時刻はロジック（60Hz）でしか
//       進まないので、剣の振り（ボーン姿勢）も 60Hz でしか変わらない。描画のレートで刻むと、
//       90Hz/144Hz では姿勢が前のフレームと同じフレームができ、プレイヤーの補間移動の分だけ
//       ずれた断面が重なって、尾が二重の線に見える。頻度をロジックに固定すれば、
//       リフレッシュレートによらず同じ尾になる。
//       ただし刻む位置は描画側の補間済み行列のまま。ロジックの行列を使うと尾が剣から離れる。
//
//       なぜ時計を2つに分けるか: 尾は「世界に残った跡」なので、消える速さは世界のスローに
//       従わせたい。一方で一時トレイルの長さは「持ち主の動作がどれだけ続くか」なので、
//       持ち主の時計に従わせたい。ラッシュ中は世界が遅く、プレイヤーだけ実時間で動くので、
//       両方を世界の時計にすると、ダッシュの尾がダッシュ後も何秒も出続けてしまう。
//       ただし animControlled（剣など）は老化も持ち主の時計にする。アニメーションが描く軌跡は
//       プレイヤーの動きと同じ速さで消えてほしいため。ヒットストップでアニメが止まれば尾も止まる。
//       時計は TimeStateComponent の localDt。持っていなければ世界全体の倍率（TimeContext）を使う。
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

private:
    /**
     * @brief Entity の時計で、このステップに進む時間を返す。
     * @param id         時計を持つはずの Entity。InvalidEntityID でもよい。
     * @param fallbackDt id が TimeStateComponent を持たないときに返す時間（秒）。
     * @return TimeStateComponent の localDt（秒）。持っていなければ fallbackDt。
     */
    float ClockDt(CCL::ECS::EntityID id, float fallbackDt) const;

    // 世界全体の倍率を掛けた dt（秒）。時計を持たないトレイルの既定。一時停止中は 0
    float WorldDt(float dt) const;
};
