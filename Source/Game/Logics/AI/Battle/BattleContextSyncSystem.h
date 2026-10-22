#pragma once
// ===========================================================================
// 概要: プレイヤーとボスを探し、BattleContext に位置などを毎フレーム書く System。
//
// 責務:
//   - BattleContext が World に無ければ追加する
//   - プレイヤーの EntityID・足元の位置・体の中心・平滑化した速度を書く
//   - ボスの EntityID と足元の位置を書く
//
// 備考:
//   BattleContext の追加もここで行う（シーン側に任せると、シーンを増やすたびに
//   書き忘れて AI の System がまとめて何もしなくなる）。
//   実行ステージは L01_WorldState。リソースへのアクセスは Read<>/Write<> の申告に載らず、
//   スケジューラが依存を検出できないので、読む側と同じステージに置くと順序が決まらない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

// テンプレート引数は申告のためだけに置いている。実際の処理は Update 内の World::ViewEach で
// プレイヤーとボスの両方を探して行う。
class BattleContextSyncSystem : public CCL::ECS::IfSystem<
                                    BattleContextSyncSystem,
                                    CCL::ECS::Read<PlayerComponent>,
                                    CCL::ECS::Read<TransformComponent>>
{
public:
    BattleContextSyncSystem() : IfSystem("BattleContextSyncSystem") {}
    void Initialize() override;
    void Update(float dt) override;

private:
    /**
     * @brief BattleContext が無ければ追加する。
     * @note GetResource は未登録だと assert で止まるので、Update からも呼んで、どの順で呼ばれても必ずある状態にする。
     */
    void EnsureContext();
};
