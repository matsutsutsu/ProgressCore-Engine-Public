#pragma once
// ===========================================================================
// 概要: ラッシュのフェーズに合わせて、カメラを寄せたり引いたりする System
//
// 責務:
//   - フェーズの構図へ、ばねで寄せる
//   - 寄せた値を、TPS カメラの構図の上書きスロットへ書く
//   - 回避成立と斬りの瞬間に、揺れと画角パンチを要求する
//
// 備考: フェーズは読むだけで、ラッシュの進行には触らない
//       カメラ側はラッシュを知らず、構図の足し込み量だけを受け取る
//       描画ステージで動くので、世界のスロー中も実時間で動く（ポーズ中は止まる）
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushCameraComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"

#include <vector>

class PlayerRushCameraSystem : public CCL::ECS::IfSystem<
    PlayerRushCameraSystem,
    CCL::ECS::Write<PlayerRushCameraComponent>,
    CCL::ECS::Read<PlayerRushComponent>>
{
public:
    PlayerRushCameraSystem() : IfSystem("PlayerRushCameraSystem") {}

    void Update(float dt) override;
    void OnGui() override;

    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

    // カメラの target が決まってから、そのカメラを探す
    std::vector<const char *> GetRunAfter() const override { return {"CameraTargetBindSystem"}; }

private:
    using Base = CCL::ECS::IfSystem<
        PlayerRushCameraSystem,
        CCL::ECS::Write<PlayerRushCameraComponent>,
        CCL::ECS::Read<PlayerRushComponent>>;

    const RushCameraShot &SelectShot(const PlayerRushCameraComponent &cam, RushPhase phase) const;

    // 構図の設定を、カメラへ渡す目標値に直す
    // target が InvalidEntityID なら注視点は寄せない
    CameraFramingParams ToFraming(const RushCameraShot &shot, float yawSign, CCL::ECS::EntityID target) const;

    // 今の構図を、ばねで目標へ1フレーム分寄せる
    // bounce が 0 なら行き過ぎずに、blendTime 秒でほぼ届く
    void SpringToward(CameraFramingParams &current, CameraFramingParams &velocity, const CameraFramingParams &goal,
                      float blendTime, float bounce, float dt) const;

    // 1つの値を、ばね（角振動数 omega・減衰比 zeta）で dt 秒進める
    void SpringStep(float &x, float &v, float goal, float omega, float zeta, float dt) const;

    // 回避の向きがカメラから見て左右どちらかで、ヨーの向き（±1）を決める
    float DecideYawSign(const PlayerRushComponent &rush, CCL::ECS::EntityID cameraID) const;

    // 斬りの瞬間の画角パンチと揺れを要求する
    void PunchStrike(const PlayerRushCameraComponent &cam, CCL::ECS::EntityID playerID, CCL::ECS::EntityID cameraID);

    // プレイヤーを映している TPS カメラを探す。なければ InvalidEntityID
    CCL::ECS::EntityID FindCamera(CCL::ECS::EntityID playerID) const;

    // 注視点を寄せる先。ラッシュの相手が生きていればその敵、いなければ InvalidEntityID
    CCL::ECS::EntityID PickFocusTarget(const PlayerRushComponent &rush) const;
};
