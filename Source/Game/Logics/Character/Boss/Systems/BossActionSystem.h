#pragma once
// ===========================================================================
// 概要: BT が出した技（BossCommandComponent）を、ボス本体の動き（速度・向き）に変える System
//
// 責務:
//   - 技に入った瞬間に、進む向きを決めてアニメの開始のトリガーを引く
//   - 待機・移動・横移動・後方ステップ・跳躍・突進・様子見・疲れを1フレーム分進める
//   - 技が硬直まで終わったら actionFinished を立て、BT に知らせる
//   - 演出中・決着後・ひるみの間は技を始めずに待機する
//
// 備考: 流れは BossCommand → BossActionSystem → CharacterMovementComponent → JoltCharacterUpdateSystem
//       BT は技を出して actionFinished を待つだけで、技の長さは BossActionComponent の段ごとの時間が決める
//       currentActionId は BossPhaseSystem が毎フレーム空にするので、BT が止まれば待機になる
// ===========================================================================
#include <random>
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Engine/Time/Components/TimeState.h"

struct BattleContext;
struct AnimStateMachineComponent; // 後方ステップの止まりのモーションの長さと、ひるみ中かを読む

class BossActionSystem : public CCL::ECS::IfSystem<
    BossActionSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Write<BossActionComponent>,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<BossCommandComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
    using Base = CCL::ECS::IfSystem<
        BossActionSystem,
        CCL::ECS::Write<CharacterMovementComponent>,
        CCL::ECS::Write<BossActionComponent>,
        CCL::ECS::Write<TransformComponent>,
        CCL::ECS::Read<BossCommandComponent>,
        CCL::ECS::Read<JoltCharacterConfigComponent>,
        CCL::ECS::Read<JoltCharacterHandleComponent>,
        CCL::ECS::Read<TimeStateComponent>>;

public:
    BossActionSystem() : IfSystem("BossActionSystem") {}
    ~BossActionSystem() override = default;

    void Update(float dt) override;
    void OnGui() override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    // 1体・1フレーム分の材料。Process 関数へまとめて渡す
    // const Frame でも、参照先（transform・movement）は書き換えられる
    struct Frame
    {
        TransformComponent                 &transform;        // 向きを書く
        CharacterMovementComponent         &movement;         // 移動速度を書く
        const JoltCharacterConfigComponent &joltConfig;       // 移動の速さの上限
        const JoltCharacterHandleComponent &joltHandle;       // 物理の位置と向き
        const BattleContext                &battle;           // ステージの中心・半径
        DirectX::SimpleMath::Vector3        playerPos;        // 相手の足元（ワールド）
        float                               dt;               // この個体の時計の dt [秒]
        const AnimStateMachineComponent    *animStateMachine; // アニメのステートの長さを引く。なければ nullptr
        bool                                raging;           // 怒りの連発中か（攻撃の技の硬直を縮める）
    };

    // 待機。足を止め、idleTurnSpeed でゆっくり相手へ向き直る（ひるみ中は向き直らない）
    void ProcessIdle(BossActionComponent &bossAction, const Frame &frame);

    // 相手へ歩いて近づく。速さは Jolt の walkSpeed
    void ProcessMove(BossActionComponent &bossAction, const Frame &frame);

    // 横移動。相手を向いたまま、入った瞬間に決めた真横へ進む
    // 動き出し → 一定の速さ → 止まり。段の移動が終わるたびに、次の段の移動を予約する
    void ProcessSideTravel(BossActionComponent &bossAction, const Frame &frame);

    // 横移動の準備。技（左へ・右へ）から真横の向きを決め、BeginTravel で動き出す
    void BeginSideTravel(ActionID actionId, float travelDistance, BossActionComponent &bossAction, const Frame &frame) const;

    // 横移動の動き方で、好きな向きへ動き出す。開始のトリガーの左右を向きから選び、
    // 距離から各段の長さを出して、加速を予約する。距離は壁の手前までに縮める
    void BeginTravel(const DirectX::SimpleMath::Vector3 &moveDir, float travelDistance, BossActionComponent &bossAction,
                     const Frame &frame) const;

    // 間合いを取る移動の向き。横（壁までの空きが広い側）＋ 距離を保つ前後 ＋ 壁から離れる内向き を足し、
    // 真横からの傾きを抑えて返す。水平で長さ 1。travelDistance は動く距離（左右の空きを測るのに使う）
    DirectX::SimpleMath::Vector3 RepositionDirection(float travelDistance, const BossActionComponent &bossAction,
                                                     const Frame &frame) const;

    // 間合いを保つ向き（正規化しない）。横（moveLeft の側）＋ 距離を保つ前後 ＋ 壁から離れる内向き を足したもの
    // 間合いを取る（RepositionDirection）と様子見（ProcessFootwork）が使う
    DirectX::SimpleMath::Vector3 KeepDistanceVector(const DirectX::SimpleMath::Vector3 &bossPos,
                                                    const DirectX::SimpleMath::Vector3 &toPlayerDir, bool moveLeft,
                                                    const BossActionComponent &bossAction, const Frame &frame) const;

    // 様子見。相手を向いたまま、間合いを保つ向きへ毎フレーム向きを決め直して動き回る
    // 決めた長さが来たら actionFinished を立てるが、止まらずに動き続ける
    void ProcessFootwork(BossActionComponent &bossAction, const Frame &frame);

    // 様子見の準備。1回の長さを下限〜上限から引き、相手の周りを回る側（空きが広い側。同じならランダム）を決める
    void BeginFootwork(BossActionComponent &bossAction, const Frame &frame);

    // 動く向きを、体から見た向き（x = 右、y = 前）に直す。moveDir が水平で長さ 1 なら、結果も長さ 1
    DirectX::SimpleMath::Vector2 BodyLocalDirection(const DirectX::SimpleMath::Vector3 &moveDir,
                                                    const TransformComponent           &transform) const;

    // 様子見のアニメの向き（前後左右）を、体から見た動く向きで選ぶ
    // 一番近い向きが、今の向きより kFootworkAnimDirHysteresis 以上近いときだけ切り替える
    BossFootworkDir FootworkAnimDir(const DirectX::SimpleMath::Vector2 &localMove, BossFootworkDir currentDir) const;

    // 後方ステップ。入った瞬間の相手の方向を向いたまま下がり、着地して硬直する
    // 下がる移動は、OnEnterState で予約した MoveTo が運ぶ
    void ProcessBackStep(BossActionComponent &bossAction, const Frame &frame);

    // 跳躍。溜め → 飛行 → 着地硬直。跳躍3種はすべてここを通り、違いは leapDef（跳び方）だけ
    // 段を見て、下の ProcessLeapWindup〜ProcessLeapLanding を呼び分ける
    void ProcessLeap(BossActionComponent &bossAction, const Frame &frame);

    // 跳躍の溜め。足を止めて相手を向き、溜めの時間が来たら踏み切る
    // 着地点はまだ決めない。溜めを見てから動いた相手に付いていかないようにする
    void ProcessLeapWindup(BossActionComponent &bossAction, const Frame &frame);

    // 跳躍の踏み切り。着地点と飛ぶ向きを決め、最初の移動を予約する
    // 滞空は頂点へ上がるところまで。踏み切りの瞬間に1回だけ呼ぶ
    void TakeOffLeap(BossActionComponent &bossAction, const Frame &frame) const;

    // 跳躍の頂点の高さ。基準の高さ（BossLeapDef::height）に、
    // 相手との距離が近いほど低く・遠いほど高くなる分を足す。0 より下にはしない
    float LeapApexHeight(float baseHeight, float distanceToPlayer) const;

    // 予約した移動が終わったときに、跳躍の次の段へ進む
    // 滞空だけは 上がる → 頂点に留まる → 降りる の3段。ほかは着地する
    void AdvanceLeapPhase(BossActionComponent &bossAction, const Frame &frame) const;

    // 跳躍の空中。跳んでいる向きへ体を向ける（滞空は相手を向く）
    void ProcessLeapAir(BossActionComponent &bossAction, const Frame &frame);

    // 跳躍の着地硬直。硬直を数え、跳躍攻撃でなければ相手へゆっくり向き直る
    void ProcessLeapLanding(BossActionComponent &bossAction, const Frame &frame);

    // 突進攻撃。下がる → 空中で狙う → 構える → 突っ込む → 走り続ける → 止まる → 硬直
    // 段の移動が終わるたびに、次の段の移動を予約する
    void ProcessDash(BossActionComponent &bossAction, const Frame &frame);

    // 技の硬直。足を止めて recoveryTime 秒数え、終わったら actionFinished を立てる
    // 横移動・後方ステップ・跳躍・突進の最後の段と、疲れの間ずっと呼ぶ
    void ProcessRecovery(BossActionComponent &bossAction, float recoveryTime, const Frame &frame);

    // 構え（Windup）に入る瞬間に1回だけ呼ぶ
    // 相手への方向を固定し、その方向へ dashDistance 進む終点と、突っ込む長さを決める
    void LockDashAim(BossActionComponent                &bossAction,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext                &battle) const;

    // 中央を挟んで相手と反対側の着地点を返す。中心からの距離は、半径 × escapeLandingRatio
    // 着地点の高さは踏み切り地点（takeoffPos）にそろえる
    DirectX::SimpleMath::Vector3 AwayToCenterLandingPoint(const BossActionComponent          &bossAction,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos,
                                                          const BattleContext                &battle) const;

    // 相手の位置から attackStandOff だけ手前の着地点。距離の上限はない
    DirectX::SimpleMath::Vector3 TowardTargetLandingPoint(const BossActionComponent          &bossAction,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos) const;

    // moveDir の向きへまっすぐ moveDistance 進んだ地点を返す（横移動・後方ステップ・突進の下がる・突っ込む）
    // 壁際でも向きは変えず、距離だけ縮めて壁の手前で止める。moveDir は水平で長さ 1
    DirectX::SimpleMath::Vector3 StraightPointInArena(const DirectX::SimpleMath::Vector3 &startPos,
                                                      const DirectX::SimpleMath::Vector3 &moveDir, float moveDistance,
                                                      const BattleContext &battle) const;

    // 点がステージの円の外なら、中心へ向かって円の縁まで引き戻す。跳躍の着地点に使う
    // 円の半径は BattleContext::arenaRadius から壁の余白を引いたもの
    DirectX::SimpleMath::Vector3 ClampInsideArena(const DirectX::SimpleMath::Vector3 &point,
                                                  const BattleContext                &battle) const;

    // BT が出した技(ActionID) を、体の動き(BossActionState) へ変換する
    // 知らない技は None
    BossActionState TranslateAction(ActionID actionId) const;

    // 跳躍の技（ActionID）に対応する跳び方を返す。跳躍の技でなければ nullptr
    // 跳躍を増やすときは、ここに1行と、BossActionComponent に BossLeapDef を1つ足す
    const BossLeapDef *LeapDefOf(ActionID actionId, const BossActionComponent &bossAction) const;

    // ひるみのアニメ（Hit_F など）を再生しているかを isFlinching に書き、明けてからの冷却を進める
    // ひるんでいる間、Update は BT の要求を読まずに待機にする
    void UpdateFlinch(BossActionComponent &bossAction, const AnimStateMachineComponent *animStateMachine, float dt) const;

    // 技に入った瞬間だけ走る処理
    // 前の技の移動の予約を取り消し、進む向きと技ごとの準備（後方ステップは移動の予約）をする
    // actionParam は技のノードの param（横移動の距離）
    void OnEnterState(BossActionState nextState, ActionID actionId, float actionParam, BossActionComponent &bossAction,
                      const Frame &frame);

    // 様子見の長さと回る側を引く乱数。Update は1体ずつ順に回すので、全個体で1つを使ってよい
    std::mt19937 m_random{std::random_device{}()};
};
