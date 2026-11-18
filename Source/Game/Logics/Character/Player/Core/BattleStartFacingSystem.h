#pragma once
// ===========================================================================
// 概要: 戦闘開始時に一度だけ、プレイヤーと TPS カメラをボスの方向へ向ける System
//
// 責務:
//   - プレイヤーとボスがそろうまで毎フレーム試し、そろったら一度だけ向きを決める
//   - プレイヤーの currentYaw をボスの方向にし、傾き（currentRoll）を 0 に戻す
//   - プレイヤーを追う TPS カメラをプレイヤーの背後へ回し、補間を飛ばして一度で合わせる
//
// 備考: カメラの向きはプレハブに保存されるので、保存したときの向きから始まらないよう、実行時に位置関係から求める
//       PlayerFacingSystem は currentYaw から回転を作るので、その前に走る
//       currentYaw は GetComponent で書いていて申告に載らないため、GetRunBefore で順序を決める
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Camera/PlayerCameraComponent.h"

#include <vector>

// フィルタはTPSカメラ。プレイヤー側は BattleContext が持つIDから直接引く
// （プレイヤーとカメラは別エンティティなので1つのフィルタには載らない）
class BattleStartFacingSystem : public CCL::ECS::IfSystem<
    BattleStartFacingSystem,
    CCL::ECS::Write<PlayerCameraComponent>>
{
public:
    BattleStartFacingSystem() : IfSystem("BattleStartFacingSystem") {}
    virtual ~BattleStartFacingSystem() = default;

    void Update(float dt) override;

    // currentYaw を読んで Transform を作る側より先に走る
    std::vector<const char *> GetRunBefore() const override { return {"PlayerFacingSystem"}; }

private:
    // プレイヤーとボスがそろっていれば向きを決める。false の間は毎フレーム試し直す
    // プレハブの生成は遅れて行われるので、開始直後はそろっていない
    bool TryApplyFacing();

    bool m_applied = false; // 向きを決め終えたか
};
