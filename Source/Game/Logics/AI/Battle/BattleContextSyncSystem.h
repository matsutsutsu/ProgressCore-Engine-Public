// BattleContext（世界の事実）にプレイヤー・ボスの居場所を毎フレーム反映する。
//
// 背景:
//   BattleContext.playerEntity/playerPos は誰かが書き込まないと
//   InvalidEntityID/ゼロ座標のままで、HasPlayer() が常に false になり、
//   これをガードにしている全システム（BossPerception, BossAction,
//   DroneFormation 等）が動かなくなる。
//   このシステムが「唯一の書き込み元」として毎フレーム更新する。
//
// 設計方針:
//   1対1なので探索コストは軽微。View を2回（Player, Boss）回すだけ。
//   実行ステージは L01_WorldState（130）。
//   BattleContext を読む最も早いシステムが DroneBrainSystem(L02_PreUpdate) なので、
//   書き手はそれより厳密に前でなければならない。L02_PreUpdate に同居させると
//   書き手と読み手の順序が決まらなくなる（リソースへのアクセスは IfSystem の
//   Read<>/Write<> 申告に載らず、スケジューラが依存を検出できないため）。
//
// リソースの所有:
//   BattleContext の登録は本システムが行う（唯一の書き込み元が所有者を兼ねる）。
//   以前は各シーンの Initialize が AddResource していたが、
//     ・シーンがAIの内部事情を知る必要が出る
//     ・シーンを増やすたびに書き忘れると、AI系が丸ごと黙って no-op になる
//   という問題があった。SquadUpdateSystem が SquadRegistry を、
//   UI 系が GameFlowState を自分で登録しているのと同じ形に揃えている。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

// PlayerComponent をフィルタに使うが、実際の更新処理は Update 内で
//        World::View を直接使い、プレイヤー・ボスの両方を探して反映する。
class BattleContextSyncSystem : public CCL::ECS::IfSystem<
                                    BattleContextSyncSystem,
                                    CCL::ECS::Read<PlayerComponent>,
                                    CCL::ECS::Read<TransformComponent>>
{
  public:
    BattleContextSyncSystem()
        : IfSystem("BattleContextSyncSystem")
    {
    }
    void Initialize() override;
    void Update(float dt) override;

  private:
    // BattleContext が未登録なら登録する。
    // @note  Update からも呼ぶ。シーン途中でリソースが失われる経路は本来無いが、
    //        GetResource は未登録だと assert で落ちるため、入口を一本化しておく。
    void EnsureContext();
};