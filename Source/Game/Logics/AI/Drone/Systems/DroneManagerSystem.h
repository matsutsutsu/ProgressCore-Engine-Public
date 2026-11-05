#pragma once
// ===========================================================================
// 概要: ドローンの生成・計数・破壊を管理する System
//
// 責務:
//   - シーン開始時に、ボスが揃い次第初期ドローンを1回だけ配備する
//   - 生存数を数えて BattleContext.droneAliveCount に書く
//   - 通し番号の割り当て（ReindexAllDrones）
//   - ドローン数を増減するデバッグ UI
//
// 備考:
//   小隊の席の割り当ては SquadUpdateSystem が持つ（生成の後に AssignNewMembers を呼ぶだけ）
//
//   droneAliveCount は「ドローン数 = ボスHP」という構想のための値。今はデバッグ表示しか読んでいない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

namespace CCL::ECS { class World;}

// 生成した機の初期値と、全機の通し番号・小隊を書くので DroneComponent は Write
class DroneManagerSystem : public CCL::ECS::IfSystem<
    DroneManagerSystem,
    CCL::ECS::Write<DroneComponent>>
{
public:
    DroneManagerSystem() : IfSystem("DroneManagerSystem") {}

    // DroneCommandSystem とデータの依存はないが、生成（構造変更）を挟むので毎回同じ順に固定する
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneCommandSystem"};
    }

    // 初期配備がまだなら配備し、生存数を BattleContext.droneAliveCount に書く
    void Update(float dt) override;

    // ---- 外部（GUI やゲームロジック）から呼ぶ操作 ----

    /**
     * @brief ドローンをボスの5m上に生成する
     * @param count 生成する機数
     * @return 実際に生成できた数。ボスがいなければ生成せず 0
     */
    int SpawnDrones(int count);

    // ドローンを先頭から count 機破壊し、実際に破壊した数を返す
    int DestroyDrones(int count);

    void DestroyAll();

    int GetAliveCount() const;

    /**
     * @brief 全ドローンの通し番号を振り直し、無所属の機を小隊へ入れる
     *
     * 撃破の後には呼ばない。振り直すと残りの全機の持ち場が一斉に変わり、削ったことが見えなくなる
     * （据え置けば落ちた機の位置が穴として残る。totalDrones が実数より多いことがあるのはこのため）
     */
    void ReindexAllDrones();

    void OnGui() override; // ドローン数の増減

private:
    // ボスが揃い次第、初期ドローンを1回だけ配備する。揃うまで毎フレーム再試行する
    void TryAutoSpawnInitial();

    // シーン内の最初のボスを探す
    CCL::ECS::EntityID FindBoss() const;

    int  m_spawnCount    = 20;    // GUI の「生成」で生成する数
    bool m_autoSpawnDone = false; // 初回配備が済んだか
};
