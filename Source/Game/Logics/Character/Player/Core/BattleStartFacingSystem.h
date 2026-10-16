#pragma once
// ===========================================================================
// 概要: 戦闘開始時に一度だけ、プレイヤーと TPS カメラをボスの方向へ向ける System。
//
// 責務:
//   - プレイヤーとボスがそろうまで毎フレーム試し、そろったら一度だけ向きを決める
//   - プレイヤーの currentYaw をボスの方向にし、傾き（currentRoll）を 0 に戻す
//   - プレイヤーを追う TPS カメラをプレイヤーの背後へ回し、補間を飛ばして一度で合わせる
//
// 備考:
//   なぜ実行時に計算するか:
//   カメラの向き(CameraBodyTPSComponent::currentYaw)はプレハブに保存されるので、
//   保存したときの向きから始まってしまう。ボスとプレイヤーの位置関係から毎回求めれば、
//   配置を動かしても、プレハブにどんな値が入っていても正しい向きから始まる。
//
//   なぜシーンではなく System か:
//   シーンに書くと、シーンを増やすたびに書き忘れる。BattleContext と同じく、
//   必要とする側が自分で解決する。
//
//   実行ステージは L02_PreUpdate。
//   BattleContextSyncSystem(L01_WorldState=130) が書いた BattleContext を読むので、それより後に走る。
//   同じステージの PlayerJumpFacingSystem は currentYaw から Transform と Jolt の回転を作るため、
//   その前に走る必要がある。currentYaw は GetComponent で書いていて Write<> の申告に
//   載らず、スケジューラが競合を検出できないので、GetRunBefore で順序を指定する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"

#include <vector>

// フィルタはTPSカメラ。プレイヤー側は BattleContext が持つIDから直接引く
// （プレイヤーとカメラは別エンティティなので1つのフィルタには載らない）。
class BattleStartFacingSystem : public CCL::ECS::IfSystem<
                                    BattleStartFacingSystem,
                                    CCL::ECS::Write<CameraBodyTPSComponent>>
{
public:
    BattleStartFacingSystem() : IfSystem("BattleStartFacingSystem") {}
    virtual ~BattleStartFacingSystem() = default;

    void Update(float dt) override;

    // currentYaw を読んで Transform を作る側より先に走る。
    std::vector<const char *> GetRunBefore() const override { return {"PlayerJumpFacingSystem"}; }

private:
    /**
     * @brief プレイヤーとボスがそろっていれば向きを決める。
     * @return 決められたか。false の間は毎フレーム試し直す
     *         （プレハブの生成は遅れて行われるので、開始直後はそろっていない）。
     */
    bool TryApplyFacing();

    bool m_applied = false; // 向きを決め終えたか
};
