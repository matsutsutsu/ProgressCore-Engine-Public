#pragma once
// ===========================================================================
// 概要: BT が出した技(BossCommandComponent)を、
//       ボス本体の体の動き（速度・向き）に変える System。
//
// 責務:
//   - currentActionId を BossActionState に変換し、
//     技に入った瞬間に進行方向などを決める
//   - 技に入った瞬間に、アニメの開始のトリガー（BackStep / Leap / Dash）を引く
//   - 待機・移動・後方ステップ・跳躍・突進を1フレーム分進め、
//     CharacterMovementComponent に書く
//   - 技が硬直まで終わったら actionFinished を立て、BT に知らせる
//   - 向きを Transform と Jolt の両方に書く
//   - 演出中と決着後は待機にする
//
// 備考:
//   流れは BossCommand → BossActionSystem → CharacterMovementComponent
//   → JoltCharacterUpdateSystem（CharacterVirtual）。
//   実行ステージは L02_PostUpdate。
//   BehaviorTreeSystem(L02_Update) の後、物理(L03_PrePhysics) の前。
//
//   技の長さは BossActionComponent の段ごとの時間が決める。
//   BT は技を出して、actionFinished が立つのを待つだけ。
//
//   currentActionId は BossPhaseSystem が毎フレーム空にするので、
//   BT が止まれば（一時停止を含む）待機(None)になる。
// ===========================================================================
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
struct AnimStateMachineComponent; // 後方ステップの止まりのモーションの長さを読む

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

    // 全ボスについて、技の切り替わりを検出し、今の動きを1フレーム分進める。
    void Update(float dt) override;

    // デバッグ用。ボスの位置をインスペクタから直接動かせる。
    void OnGui() override;

    /**
     * @brief テンプレート引数以外に読む型を申告する。
     *
     * Update が AnimStateMachineComponent（後方ステップの
     * 止まりのモーションの長さ）を GetComponent で読むので、申告だけを足す。
     * テンプレート引数には足さない。ForEach の絞り込みにもなるので、
     * アニメを持たないボスで、この System が何も言わずに動かなくなる。
     */
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // Update が技の開始のトリガーを AnimParametersComponent へ
    // GetComponent で書くので、申告だけを足す（理由は GetReadTypes と同じ）
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    // 1体・1フレーム分の材料。Process 関数へまとめて渡す。
    // const Frame でも、参照先（trans・movement）は書き換えられる
    struct Frame
    {
        TransformComponent                 &trans;      // 向きを書く
        CharacterMovementComponent         &movement;   // 移動速度を書く
        const JoltCharacterConfigComponent &config;     // 移動の速さの上限
        const JoltCharacterHandleComponent &joltHandle; // 物理の位置と向き
        const BattleContext                &battle;     // ステージの中心・半径
        DirectX::SimpleMath::Vector3        playerPos;  // 相手の足元（ワールド）
        float                               dt;         // この個体の時計の dt [秒]
        const AnimStateMachineComponent    *anim;       // アニメのステートの長さを引く。無ければ nullptr
    };

    // 待機。足を止め、idleTurnSpeed でゆっくり相手へ向き直る。
    void ProcessIdle(BossActionComponent &bossAction, const Frame &f);

    // 相手へ歩いて近づく。速さは Jolt の walkSpeed。
    void ProcessMove(BossActionComponent &bossAction, const Frame &f);

    // 横歩き（様子見）。相手への方向の直角へ進み、相手を中心に円を描く。
    // 向きは進行方向ではなく相手へ向け続ける。
    // 壁際では回る向きを内側へ反転する。
    void ProcessStrafe(BossActionComponent &bossAction, const Frame &f);

    // 後方ステップ。入った瞬間の相手の方向を向いたまま下がり、着地して硬直する
    // 下がる移動は、OnEnterState で予約した MoveTo が運ぶ
    void ProcessBackStep(BossActionComponent &bossAction, const Frame &f);

    // 跳躍。溜め → 飛行 → 着地硬直。跳躍3種はすべてここを通り、違いは着地点だけ
    // 踏み切りの瞬間に飛ぶ移動を予約し、滞空は 上がる → 留まる → 降りる をつなぐ
    void ProcessLeap(BossActionComponent &bossAction, const Frame &f);

    // 突進攻撃。下がる → 空中で狙う → 構える → 突っ込む → 走り続ける → 止まる → 硬直
    // 段の移動が終わるたびに、次の段の移動を予約する
    void ProcessDash(BossActionComponent &bossAction, const Frame &f);

    // 技の硬直。足を止めて recoveryTime 秒数え、終わったら actionFinished を立てる
    // 後方ステップ・跳躍・突進の最後の段で呼ぶ
    void ProcessRecovery(BossActionComponent &bossAction, float recoveryTime, const Frame &f);

    // 構え（Windup）に入る瞬間に1回だけ呼ぶ。
    // 相手への方向を固定し、その方向へ dashDistance 進む終点と、突っ込む長さを決める。
    void LockDashAim(BossActionComponent                &bossAction,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const BattleContext                &battle) const;

    // 踏み切りの瞬間に1回だけ呼ぶ。
    // 着地点（ステージ内に収めた値）と飛ぶ向きを決め、着地点を返す。
    DirectX::SimpleMath::Vector3 BeginLeapFlight(BossActionComponent                &bossAction,
                                                 const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                 const DirectX::SimpleMath::Vector3 &playerPos,
                                                 const BattleContext                &battle) const;

    /**
     * @brief 中央を挟んで相手と反対側の着地点を返す。
     *
     * 中心からの距離は 半径×escapeLandingRatio。
     *
     * @param bossAction    escapeLandingRatio を持つボスの行動。
     * @param takeoffPos 踏み切り地点（ワールド）。着地点の高さをこれにそろえる。
     * @param playerPos  相手の位置（ワールド）。
     * @param battle     ステージの情報（中心・半径）。
     * @return 着地点（ワールド）。
     */
    DirectX::SimpleMath::Vector3 AwayToCenterLandingPoint(const BossActionComponent          &bossAction,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos,
                                                          const BattleContext                &battle) const;

    // 相手の位置から attackStandOff だけ手前の着地点。距離の上限は無い。
    DirectX::SimpleMath::Vector3 TowardTargetLandingPoint(const BossActionComponent          &bossAction,
                                                          const DirectX::SimpleMath::Vector3 &takeoffPos,
                                                          const DirectX::SimpleMath::Vector3 &playerPos) const;

    // dir の向きへまっすぐ distance 進んだ地点を返す（後方ステップ・突進の下がる・突っ込む）
    // 壁際でも向きは変えず、距離だけ縮めて壁の手前で止める。dir は水平で長さ 1
    DirectX::SimpleMath::Vector3 StraightPointInArena(const DirectX::SimpleMath::Vector3 &from,
                                                      const DirectX::SimpleMath::Vector3 &dir, float distance,
                                                      const BattleContext &battle) const;

    /**
     * @brief 点がステージの円の外なら、中心へ向かって円の縁まで引き戻す。
     *
     * 跳躍の着地点と、横歩きの先読みに使う。
     *
     * @param point  引き戻す点（ワールド）。
     * @param battle ステージの情報。
     *               円の半径は BattleContext::arenaRadius − 壁からの余白。
     * @return ステージの円の内側に収めた点。
     */
    DirectX::SimpleMath::Vector3 ClampInsideArena(const DirectX::SimpleMath::Vector3 &point,
                                                  const BattleContext                &battle) const;

    // BT が出した技(ActionID) を、体の動き(BossActionState) へ変換する。
    // 知らない技は None。
    BossActionState TranslateAction(ActionID actionId) const;

    // 技に入った瞬間だけ走る処理
    // 前の技の移動の予約を取り消し、進む向きと技ごとの準備（後方ステップは移動の予約）をする
    void OnEnterState(BossActionState next, ActionID actionId, BossActionComponent &bossAction, const Frame &f) const;
};
