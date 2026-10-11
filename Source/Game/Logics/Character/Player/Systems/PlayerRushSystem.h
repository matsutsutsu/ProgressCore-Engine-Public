#pragma once
// ===========================================================================
// 概要: ジャスト回避成立後の「ラッシュ」を駆動する System。
//
// 責務:
//   - ジャスト回避の成功(JustEvadeSuccessEvent)を受けて、ラッシュを開始する
//   - ラッシュ中は世界を一律のスローにし、プレイヤーだけ実時間へ戻す
//   - 操作フェーズ（操作不能/準備/自由）を順に進め、自由が終わったらラッシュを終える
//   - ラッシュ中かどうかを画面演出(JustEvadeEffectComponent)へ伝える
//   - 準備フェーズの攻撃入力で、敵の方へ踏み込む（間合い詰め）
//
// 備考: 回避ダッシュ本体と歩走は PlayerDashSystem（別System）。
//       ビネットのフェードと描画は JustEvadeEffectSystem(R02)。
//       間合い詰めの詰め先は、ロックオン中ならその敵、無ければ今キャラが向いている方向。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Player/Components/RushComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"

#include <vector>

class PlayerRushSystem : public CCL::ECS::IfSystem<PlayerRushSystem,
                                                   CCL::ECS::Write<RushComponent>,
                                                   CCL::ECS::Write<TimeStateComponent>,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Read<PlayerComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerRushSystem() : IfSystem("PlayerRushSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    // 画面演出のシングルトン(JustEvadeEffectComponent)へ書くので、Filter 外の Write を申告する。
    // 宣言しないと、同じ型を触る他 System との並列実行で競合を検出できない。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // 移動(CharacterMovementComponent)を PlayerDashSystem と同じ L02_Update で書くので、順番を宣言する。
    // 踏み込みは回避ダッシュより後に書き、ラッシュ中はこちらの指令を優先する。
    std::vector<const char *> GetRunAfter() const override { return {"PlayerDashSystem"}; }

private:
    // 基底の Write 申告（RushComponent / TimeStateComponent / CharacterMovementComponent）を
    // 足し込むためのエイリアス。
    using Base = CCL::ECS::IfSystem<PlayerRushSystem,
                                    CCL::ECS::Write<RushComponent>,
                                    CCL::ECS::Write<TimeStateComponent>,
                                    CCL::ECS::Write<CharacterMovementComponent>,
                                    CCL::ECS::Read<PlayerComponent>,
                                    CCL::ECS::Read<PlayerInputComponent>,
                                    CCL::ECS::Read<TransformComponent>>;

    // ラッシュを開始する（操作不能フェーズから始める）。
    void StartRush(RushComponent &rush);

    /**
     * @brief 今のフェーズの残り時間が尽きたときに、次のフェーズへ進める。
     *
     * 操作不能 → 準備 → 自由 の順に進み、自由が終わったらラッシュを終える。
     *
     * @param rush ラッシュの状態（phase / timer を書く）。
     * @param time プレイヤーの時間の状態（終了時に cinematicScale を戻す）。
     */
    void AdvancePhase(RushComponent &rush, TimeStateComponent &time);

    /**
     * @brief ラッシュ中の時間制御を1フレーム進める。
     *
     * 一律の世界スローを維持しつつ、cinematicScale でプレイヤーだけ実時間へ戻す。
     *
     * @param rush    ラッシュの状態（worldScale / playerScale を読む）。
     * @param time    プレイヤーの時間の状態（cinematicScale を書く。group を読む）。
     * @param timeCtx 全体の時間倍率。nullptr なら等速とみなす。
     */
    void DriveTimeScale(RushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

    // ラッシュを終える（スローの要求をやめ、プレイヤーの時間倍率を戻し、踏み込みも止める）。
    void EndRush(RushComponent &rush, TimeStateComponent &time);

    // 操作不能フェーズ中、プレイヤーをその場で止める（移動の意思を打ち消す）。
    void StopPlayer(CharacterMovementComponent &movement);

    /**
     * @brief ラッシュ中の攻撃入力で、敵の方へ踏み込む（間合い詰め）。
     *
     * 攻撃を押した瞬間に詰め先を決める。ロックオン中ならその敵、無ければ今キャラが
     * 向いている方向。続く closeInMaxTime の間 overrideSpeed で前進させる。
     * ロックオン中は毎フレーム敵の位置へ向け直し、closeInStopDistance まで来たら止まる。
     *
     * @param rush      ラッシュの状態（closeInTimer / closeInDir / closeInTarget を書く）。
     * @param movement  動かす側への指令（desiredVelocity / overrideSpeed を書く）。
     * @param input     攻撃入力（attackPressed を読む）。
     * @param player    ロックオン先と現在の向きの参照元（lockOnTargetID / currentYaw を読む）。
     * @param playerPos プレイヤーのワールド位置。敵との距離・方向を測るのに使う。
     * @param dt        経過時間（秒）。
     */
    void DriveCloseIn(RushComponent &rush, CharacterMovementComponent &movement,
                      const PlayerInputComponent &input, const PlayerComponent &player,
                      const DirectX::SimpleMath::Vector3 &playerPos, float dt);

    // 間合い詰めの詰め先を選ぶ。ロックオン中で生存している敵。無ければ InvalidEntityID。
    CCL::ECS::EntityID PickCloseInTarget(const PlayerComponent &player) const;

    // 詰め先がまだ有効（存在し生存中）かを返す。
    bool IsCloseInTargetAlive(CCL::ECS::EntityID id) const;

    // ジャスト回避の成功で届いた「開始したいプレイヤー」。別タスクで届くので溜めて Update で開始する。
    std::vector<CCL::ECS::EntityID> m_pendingStarts;
};
