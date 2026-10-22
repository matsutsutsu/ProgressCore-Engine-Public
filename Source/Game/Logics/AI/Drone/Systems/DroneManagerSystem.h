#pragma once
// ===========================================================================
// 概要: ドローンの生成・計数・破壊を管理する System。
//
// 責務:
//   - シーン開始時に、ボスが揃い次第初期ドローンを1回だけ配備する
//   - 生存数を数えて BattleContext.droneAliveCount に書く
//   - 通し番号の割り当て（ReindexAllDrones）
//   - ドローン数を増減するデバッグ UI
//
// 備考:
//   陣形計算は DroneFormationSystem、個体の判断は DroneBrainSystem が担う。
//   小隊の席の割り当てと小隊の調整 UI は SquadUpdateSystem が持つ（生成の後に AssignNewMembers を呼ぶだけ）。
//
//   droneAliveCount は「ドローン数 = ボスHP」という構想のための値だが、現時点で
//   読むのはデバッグ表示だけで、BT の条件にも他の System にも繋がっていない。
//   ドローンを減らす経路も無い（Drone_Guide.md §9）ので、値は常に初期配備数のまま。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

namespace CCL::ECS { class World;}

// DroneComponent が Write なのは、Update の初回配備（TryAutoSpawnInitial → SpawnDrones）が
// 生成した機の DroneComponent と、ReindexAllDrones で全機の通し番号・小隊を書き換えるため
// （小隊の席は SquadUpdateSystem::AssignNewMembers が、この System の中で書く）。
class DroneManagerSystem : public CCL::ECS::IfSystem<DroneManagerSystem,
                                                     CCL::ECS::Write<DroneComponent>>
{
public:
    DroneManagerSystem() : IfSystem("DroneManagerSystem") {}

    /**
     * @brief 同じ L02_Update に居る他のドローン系との前後を宣言する。
     *
     * スポーン時に ReindexAllDrones() が localIndex / totalDrones / squadId / slotInSquad を書き、陣形はそれを読んで持ち場の座標を出す。
     * 順序が入れ替わると、配備した最初のフレームだけ古い通し番号で円が組まれる。
     *
     * DroneCommandSystem との間に実データ依存は無い。
     * ただし生成（構造変更）を挟むので、毎回同じ順で走るよう固定しておく。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneCommandSystem"};
    }

    /**
     * @brief 毎フレーム、生存ドローンを数えて BattleContext.droneAliveCount に書く。
     * @note 初期配備が済んでいなければ、先に TryAutoSpawnInitial で配備する。
     */
    void Update(float dt) override;

    // ---- 外部（GUI やゲームロジック）から呼ぶ操作 ----

    /**
     * @brief ドローンをボスの5m上に生成する。
     * @param count 生成する機数。
     * @return 実際に生成できた数。ボスが居なければ生成せず 0。
     */
    int SpawnDrones(int count);

    // ドローンを先頭から count 機破壊し、実際に破壊した数を返す。
    int DestroyDrones(int count);

    void DestroyAll();

    // 現在の生存数を返す（ViewEach で数える）。
    int GetAliveCount() const;

    /**
     * @brief 全ドローンの localIndex / totalDrones を振り直し、無所属の機を小隊へ入れる（SquadUpdateSystem::AssignNewMembers）。
     *
     * 撃破の後には呼ばない。番号を振り直すと生き残った全機の持ち場が同時に変わり、
     * 1機落ちるたびに残りの全機が円周上を移動して並び直すため、削ったことが見えなくなる。
     * 据え置けば落ちた機の位置が穴として残る（totalDrones が実数より多いのはこのため）。
     * 小隊の欠員は SquadUpdateSystem が毎フレーム数え直して詰める。
     */
    void ReindexAllDrones();

    void OnGui() override; // ドローン数の増減

private:
    /**
     * @brief ボスが準備でき次第、初期ドローンを1回だけ配備する。
     *
     * シーン開始直後はボス未生成のフレームがあるため、揃うまで毎フレーム再試行する。
     */
    void TryAutoSpawnInitial();

    // シーン内の最初のボス（BossCommandComponent と TransformComponent を持つ）を探す。
    CCL::ECS::EntityID FindBoss() const;

    int  m_spawnCount    = 20;    // GUI の「生成」で生成する数
    bool m_autoSpawnDone = false; // 初回配備が完了したか（1回だけ実行するためのラッチ）
};
