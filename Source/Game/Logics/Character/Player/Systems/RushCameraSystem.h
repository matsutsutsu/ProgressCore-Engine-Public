#pragma once
// ===========================================================================
// 概要: ラッシュのフェーズに合わせて、カメラの寄りと引きを演出する System。
//
// 責務:
//   - RushComponent のフェーズから、そのフェーズの構図（RushCameraComponent の設定）を選ぶ
//   - 選んだ構図へ、フェーズごとの時間と行き過ぎ（ばね）で寄せる
//   - 寄せた値を、プレイヤーを映す TPS カメラの構図の上書きスロットへ書く
//   - 回避成立と斬りの瞬間に、揺れと画角パンチを要求する
//   - ラッシュ終了後は releaseTime で効きを落とし、スロットを手放す
//
// 備考: フェーズを進めるのは PlayerRushSystem（ロジック）。この System はフェーズを読むだけで、
//       ラッシュの進行には触らない。カメラはラッシュのことを知らず、構図の足し込みだけを受け取る。
//       描画ステージで動くので、世界のスロー中も実時間でカメラが動く（ポーズ中は止まる）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/RushCameraComponent.h"
#include "Game/Logics/Character/Player/Components/RushComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

#include <vector>

class RushCameraSystem : public CCL::ECS::IfSystem<RushCameraSystem,
                                                   CCL::ECS::Write<RushCameraComponent>,
                                                   CCL::ECS::Read<RushComponent>,
                                                   CCL::ECS::Read<PlayerComponent>>
{
public:
    RushCameraSystem() : IfSystem("RushCameraSystem") {}

    void Update(float dt) override;

    // 今の構図と効きをインスペクタに出す（値の調整は Rush Camera Component 側で行う）。
    void OnGui() override;

    // Filter 外へ書く型（カメラの構図の上書きスロット）を申告する。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // Filter 外から読む型（画角パンチの基準にするカメラの今の画角）を申告する。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // カメラの target が解決されてから、そのカメラを探す。
    std::vector<const char *> GetRunAfter() const override { return {"CameraTargetBindSystem"}; }

private:
    // 基底の Read / Write 申告を足し込むためのエイリアス。
    using Base = CCL::ECS::IfSystem<RushCameraSystem,
                                    CCL::ECS::Write<RushCameraComponent>,
                                    CCL::ECS::Read<RushComponent>,
                                    CCL::ECS::Read<PlayerComponent>>;

    // フェーズに対応する構図の設定を返す。
    const RushCameraShot &SelectShot(const RushCameraComponent &cam, RushPhase phase) const;

    /**
     * @brief 構図の設定を、カメラへ出す要求の形（目標値）に移す。
     *
     * @param shot    フェーズの構図。
     * @param yawSign ヨーと傾きに掛ける左右（開始時に回避方向から決めた値）。
     * @param target  注視点を寄せる先（ロックオン中の敵）。居なければ InvalidEntityID（寄せない）。
     * @return 構図の目標値。
     */
    CameraFramingParams ToFraming(const RushCameraShot &shot, float yawSign, CCL::ECS::EntityID target) const;

    /**
     * @brief 今の構図を、ばねで目標へ1フレーム分寄せる。
     *
     * bounce=0 なら行き過ぎずに blendTime 秒でほぼ届き、bounce を上げるほど行き過ぎて戻る。
     *
     * @param current   今の構図（書き換える）。
     * @param velocity  各値の動く速さ（ばねの状態。書き換える）。
     * @param goal      目標の構図。
     * @param blendTime 寄せきる時間（秒）。
     * @param bounce    行き過ぎの強さ（0〜0.8）。
     * @param dt        経過時間（秒）。
     */
    void SpringToward(CameraFramingParams &current, CameraFramingParams &velocity, const CameraFramingParams &goal,
                      float blendTime, float bounce, float dt) const;

    // 1つの値を、ばね（角振動数 omega・減衰比 zeta）で目標へ dt 秒分進める。速すぎる dt は刻んで安定させる。
    void SpringStep(float &x, float &v, float goal, float omega, float zeta, float dt) const;

    // ラッシュ開始時に、回避方向がカメラから見て左右どちらかでヨーの向きを決める。
    float DecideYawSign(const RushComponent &rush, CCL::ECS::EntityID cameraID) const;

    // プレイヤーを映している TPS カメラを探す。無ければ InvalidEntityID。
    CCL::ECS::EntityID FindCamera(CCL::ECS::EntityID playerID) const;

    // 斬りの瞬間の画角パンチと揺れを要求する。
    void PunchStrike(const RushCameraComponent &cam, CCL::ECS::EntityID playerID, CCL::ECS::EntityID cameraID);

    // 注視点を寄せる先（ロックオン中で生存している敵）。居なければ InvalidEntityID。
    CCL::ECS::EntityID PickFocusTarget(const PlayerComponent &player) const;
};
