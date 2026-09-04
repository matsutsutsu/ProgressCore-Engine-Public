// 突撃中のドローンの「当たり判定を開け閉めする」システム。
//
// 役割分担（判断・実行・当たり判定）:
//   DroneBrainSystem       … いつ攻撃するか(currentState=FireCharge)を決める
//   DroneMovementSystem    … タメ→リリースの物理挙動を動かす
//   DroneHitSystem(本ファイル) … 危険な区間だけ Hitbox を有効にする ← ここ
//   HitboxCollisionSystem  … Jolt の接触報告を受けて DamageEvent を積む
//
// なぜ自前の距離判定をやめたか:
//   以前はここで「プレイヤーとの距離」を毎フレーム測っていた。理屈は単純だが、
//   実際には当たらなかった。原因が積み重なっていたため。
//     ・Transform.position はローカル座標なのに、それをそのまま比べていた
//     ・プレイヤーの基準点が足元で、身長 9m ぶん下を測っていた
//     ・突撃速度 165m/s では 1フレームに 2.75m 進み、点の判定では跨いで通り過ぎる
//   どれも「座標系と離散化」の問題で、剣の判定が既に Jolt で解いているものと同じ。
//   同じ問題を2箇所で別々に解くと、片方だけ壊れて原因が分からなくなる。
//   ドローンにセンサー剛体を持たせて既存の経路へ合流させれば、当たり判定は1本になる。
//
// コストについて:
//   「1000機ぶんの剛体は過大」という判断で以前は距離判定を選んだ。実際の同時出撃は
//   12機(DroneManagerSystem::m_spawnCount)なので、その前提はもう成り立っていない。
//   機数を大きく増やすときはここを再検討すること。
#pragma once
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/Combat/CombatComponents.h"

// 危険な区間だけドローンの Hitbox を有効にする。
class DroneHitSystem : public CCL::ECS::IfSystem<DroneHitSystem,
                           CCL::ECS::Read<DroneComponent>,
                           CCL::ECS::Write<HitboxComponent>>
{
public:
    DroneHitSystem() : IfSystem("DroneHitSystem") {}

    // 判定の開け閉めは、その突撃の状態が確定した後でなければ 1フレームずれる。
    //   DroneMovementSystem が stateTimer を進めて「タメ→リリース」を切り替えるので、
    //   その後に読む。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneBrainSystem", "DroneMovementSystem"};
    }

    void Update(float dt) override;

    // 判定が開いている機数を出す。当たらないときに
    // 「判定が閉じている」のか「形状が届いていない」のかを切り分けるため。
    void OnGui() override;

  private:
    int m_droneCount   = 0;
    int m_activeCount  = 0;
    int m_stateCount[5] = {0, 0, 0, 0, 0};
};
