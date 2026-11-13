#pragma once
#include <SimpleMath.h>

#include "BossActionCommon.h"

// ボスの跳躍（中央へ逃げる・跳躍攻撃・滞空）。溜め → 飛行 → 着地硬直
// 3種の違いは leapDef（跳び方）だけ
class BossLeapAction
{
public:
    // 技の跳び方（TranslateAction が選んだもの）を写し、溜めから始める
    void Enter(const BossLeapDef *leapDef, BossActionComponent &bossAction) const;

    // 今の段を1フレーム分進める
    void Update(BossActionComponent &bossAction, const BossActionFrame &frame) const;

private:
    // 溜め。足を止めて相手を向き、溜めの時間が来たら踏み切る
    void UpdateWindup(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 空中。跳んでいる向きへ体を向ける（滞空は相手を向く）
    void UpdateAir(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 着地硬直。硬直を数え、跳躍攻撃でなければ相手へゆっくり向き直る
    void UpdateLanding(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 踏み切り。着地点と飛ぶ向きを決め、最初の移動を予約する
    // 踏み切りの瞬間に1回だけ呼ぶ。着地点はここで決め、以後は相手を追わない
    void TakeOff(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 跳躍攻撃の、段の切れ目の位置と縦の速さ
    struct AttackLeapPlan
    {
        DirectX::SimpleMath::Vector3 hangStartPos   = DirectX::SimpleMath::Vector3::Zero; // ゆっくりになり始める位置（上りの終点）
        DirectX::SimpleMath::Vector3 hangEndPos     = DirectX::SimpleMath::Vector3::Zero; // ゆっくりが終わる位置（降りの始点）
        float                        hangSpeed      = 0.0f; // ゆっくりの出入りの縦の速さ [m/s]
        float                        riseStartSpeed = 0.0f; // 踏み切りの縦の速さ [m/s]
        float                        riseTime       = 0.0f; // 上りの時間 [秒]（降りと同じ）
    };

    // 跳躍攻撃の段の切れ目を決める。縦は 上り → ゆっくり（弱い重力）→ 降り（上りの鏡写し）
    // leapTakeoffPos と leapLandingPos を決めた後に呼ぶ
    AttackLeapPlan PlanAttackLeap(const BossActionComponent &bossAction, float apexHeight) const;

    // 予約した移動が終わったときに、次の段へ進む
    // 跳躍攻撃・滞空は 上がる → 頂点の段 → 降りる。逃げるはそのまま着地する
    void AdvancePhase(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 相手との距離が、近い〜遠いのどこにあるか（0〜1）。頂点の高さに使う
    float FarRatio(float distanceToPlayer) const;

    // 頂点の高さ。基準の高さに、相手が遠いほど多く足す。0 より下にはしない
    float ApexHeight(float baseHeight, float farRatio) const;

    // 跳躍の種類ごとの着地点。ステージの内側に収め、高さは踏み切りの地面
    DirectX::SimpleMath::Vector3 LandingPoint(const BossActionComponent &bossAction, const DirectX::SimpleMath::Vector3 &takeoffPos,
                                              const BossActionFrame &frame) const;

    // 中央へ逃げるの着地点。中央を挟んで相手と反対側で、中心からの距離は 半径 × escapeLandingRatio
    DirectX::SimpleMath::Vector3 EscapeLandingPoint(const BossActionComponent          &bossAction,
                                                    const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                    const DirectX::SimpleMath::Vector3 &playerPos,
                                                    const BattleContext                &battle) const;

    // 跳躍攻撃の着地点。相手の位置から attackStandOff だけ手前。距離の上限はない
    DirectX::SimpleMath::Vector3 AttackLandingPoint(const BossActionComponent          &bossAction,
                                                    const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                    const DirectX::SimpleMath::Vector3 &playerPos) const;
};
