#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュ、ジャスト回避後の接近、歩きと走りの切り替えを処理する System。
//
// 責務:
//   - ダッシュの開始・継続・終了と、ジャスト回避の受付時間を管理する
//   - ジャスト回避成立後のラッシュ（踏み込み＋集中スロー）を駆動し、世界スローを打ち消す
//   - 回避中は HealthComponent の無敵タイマーを保つ
//   - ダッシュの後は走行に移り、無入力が続いたら歩きに戻す
//   - ダッシュ中はカメラへ画角を広げる要求を書き、ルートモーションの移動を止める
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"

// ポインタでしか使わないので前方宣言にする（.cpp で include する）。
struct RootMotionComponent;

class PlayerDashSystem : public CCL::ECS::IfSystem<PlayerDashSystem,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<DashComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<TransformComponent>,
                                                   CCL::ECS::Write<TimeStateComponent>>
{
public:
    PlayerDashSystem() : IfSystem("PlayerDashSystem") {}
    void Update(float dt) override;

    // テンプレート引数ぶんに加え、ラッシュのビネット演出を切り替える JustEvadeEffectComponent(singleton)
    // への書き込みを申告する。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    // 回避（ダッシュ/接近）中は、被弾処理が見る HealthComponent.invincibilityTimer を invincibleWindow 以上に保つ。
    void MaintainInvincibility(CCL::ECS::EntityID id, const DashComponent &dash);

    /**
     * @brief ジャスト回避成立後のラッシュ（BotW風）を処理する。
     *
     * フェーズA(踏み込み): 操作をロックし、回避方向へ ease-out の減少速度で踏み込む。
     * フェーズB(集中スロー): 速度の上書きをやめ、入力を通して自由行動させる。
     * どちらも世界をスローにしたまま、cinematicScale でプレイヤーだけ実時間へ戻す。
     * 時間は rawDt（スローの掛からない実時間）で数える。
     *
     * @param rawDt    実時間の経過（秒）。フェーズの残り時間を進める。
     * @param timeCtx  全体スローの倍率。nullptr ならプレイヤーのスロー打ち消しを行わない。
     * @param dash     ラッシュの状態を持つダッシュ。
     * @param movement 移動速度を書く先（フェーズAのみ上書きする）。
     * @param time     プレイヤーの時間の状態（cinematicScale を書く）。
     * @param rm       ルートモーション。nullptr 可。
     * @return フェーズA（操作ロック）なら true。呼び出し側はこのフレームの残りの処理を飛ばす。
     *         フェーズB（自由行動）は false を返し、通常のダッシュ/移動処理を続けさせる。
     */
    bool UpdateFlurry(float rawDt, const TimeContext *timeCtx,
                      DashComponent &dash, CharacterMovementComponent &movement,
                      TimeStateComponent &time, RootMotionComponent *rm);

    /**
     * @brief ダッシュの開始・継続・終了を処理する。終了時は歩きに戻さず走行に移す。
     *
     * @param dash        ダッシュの状態。
     * @param player      プレイヤーの状態。
     * @param dashPressed このフレームにダッシュの入力があったか。
     * @param movement    移動速度を書く先。
     * @param trans       プレイヤーの Transform。
     * @param time        プレイヤーの時間の状態（localDt でタイマーを進める）。
     * @param id          プレイヤーのエンティティ。
     * @return このフレームをダッシュとして処理したら true（呼び出し側は歩き/走りの処理を行わない）。
     */
    bool UpdateDash(DashComponent &dash, PlayerComponent &player, bool dashPressed,
                    CharacterMovementComponent &movement, const TransformComponent &trans,
                    const TimeStateComponent &time, CCL::ECS::EntityID id);

    // ダッシュしていないときの歩き/走り。走行中は、無入力が runIdleReleaseTime 続いたら歩きに戻す。
    void UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt);

    // このプレイヤーを映す TPS カメラへ、画角を広げる要求を書く。
    void PushWideFovRequest(CCL::ECS::EntityID playerID, bool wide);

    // ラッシュを終え、プレイヤーの時間倍率を元に戻す（世界スローは呼ぶのをやめれば自然にイーズ復帰）。
    void EndFlurry(DashComponent &dash, TimeStateComponent &time);

    // ラッシュのビネット演出を切り替える（JustEvadeEffectComponent.active を叩く）。
    void SetFlurryVignette(bool on);

    // ラッシュがビネットを点けた“持ち主”か。ラッシュ中は true にし、抜けた最初のフレームで
    // 一度だけ false にして降ろす（＝フェードアウト開始）。毎フレーム false を書かないのは、
    // 切り払い側が立てたビネットを横から消さないため。
    bool m_flurryVignetteOwned = false;
};
