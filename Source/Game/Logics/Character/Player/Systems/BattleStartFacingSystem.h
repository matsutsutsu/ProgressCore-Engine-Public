// 戦闘開始時に一度だけ、プレイヤーとTPSカメラをボスの方向へ向ける。
//
// なぜ実行時に計算するか:
//   プレイヤーの向き(PlayerComponent::currentYaw)もカメラの向き
//   (CameraBodyTPSComponent::currentYaw)も、プレイ中の値がそのままプレハブへ
//   保存される実行時状態になっている。実際 Player.json には -48.23(rad) が、
//   PlayerFollowCamera.json には -252.14(deg) が焼き込まれており、開始直後に
//   プレイヤーが横を向き、カメラが明後日を映していた。
//   ボスとプレイヤーの位置関係から毎回求め直せば、配置を動かしても、
//   プレハブに何が焼かれていても正しい向きから始まる。
//
// なぜシーンではなくシステムか:
//   「どれがボスか」はゲームの知識だが、シーンに書くとシーンを増やすたびに
//   書き忘れる。BattleContext と同じく、必要とする側が自分で解決する。
//
// 実行ステージは L02_PreUpdate。
//   BattleContext(L01_WorldState=130) を読むので、それより後でなければならない。
//   同ステージの PlayerMoveSystem は currentYaw から Transform とJoltの回転を作るため、
//   本システムはその前に走る必要がある。ただし currentYaw への書き込みは
//   BattleContext 経由の GetComponent で行っており Write<> 申告に載らない＝
//   スケジューラが競合を検出できないので、GetRunBefore で明示する。
#pragma once
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
    BattleStartFacingSystem()
        : IfSystem("BattleStartFacingSystem")
    {
    }
    virtual ~BattleStartFacingSystem() = default;

    void Update(float dt) override;

    // currentYaw を読んで Transform を作る側より先に走る。
    std::vector<const char *> GetRunBefore() const override { return {"PlayerMoveSystem"}; }

private:
    /// プレイヤーとボスが揃っていれば向きを確定させる。
    /// @return 確定できたか。false の間は毎フレーム再試行する
    ///         （プレハブ生成は遅延されるので開始直後は揃っていない）。
    bool TryApplyFacing();

    bool m_applied = false;
};
