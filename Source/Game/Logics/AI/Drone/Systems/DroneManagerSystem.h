#pragma once
// ===========================================================================
// 概要: ドローンの生成・計数・破壊を管理する System
//
// 責務:
//   - シーン開始時に、ボスが揃い次第初期ドローンを1回だけ配備する
//   - 第2形態の移行ムービーのシグナル（DroneReinforce）で、増援を1回だけ配備する
//     一度に出さず少しずつ出して、ボスから湧き出す流れに見せる
//     ムービーを飛ばしたときは、第2形態に入った時点で配備する
//   - 生存数を数えて BattleContext.droneAliveCount に書く
//   - 通し番号の割り当て（ReindexAllDrones）
//   - ドローン数を増減するデバッグ UI
//
// 備考:
//   小隊の席の決め方は SquadUpdateSystem が持つ（番号を振るたびに AssignSeat を呼ぶだけ）
//
//   droneAliveCount は「ドローン数 = ボスHP」という構想のための値。今はデバッグ表示しか読んでいない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"

namespace CCL::ECS { class World;}

// 生成した機の初期値と、全機の通し番号・小隊を書くので DroneComponent は Write
// 増援の予備のきっかけに、ボスの形態を読む
class DroneManagerSystem : public CCL::ECS::IfSystem<
    DroneManagerSystem,
    CCL::ECS::Write<DroneComponent>,
    CCL::ECS::Read<BossPhaseComponent>>
{
public:
    // 移行ムービーの台本（Assets/Cinematics/BossPhase2.json）に置くシグナルの名前
    static constexpr const char *kReinforceSignal = "DroneReinforce";

    DroneManagerSystem() : IfSystem("DroneManagerSystem") {}

    // 増援のシグナルを受ける
    void Initialize() override;

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
     * @brief 全ドローンの通し番号を振り直し、番号から小隊と席を決める
     *
     * 撃破の後には呼ばない。振り直すと残りの全機の持ち場が一斉に変わり、削ったことが見えなくなる
     * （据え置けば落ちた機の位置が穴として残る。totalDrones が実数より多いことがあるのはこのため）
     */
    void ReindexAllDrones();

    void OnGui() override; // ドローン数の増減

private:
    // ボスが揃い次第、初期ドローンを1回だけ配備する。揃うまで毎フレーム再試行する
    void TryAutoSpawnInitial();

    // シグナルが届いたか、ボスが第2形態に入ったら、増援を1回だけ配備する
    // 始めた後は、決めた間隔ごとに少しずつ出す
    void TryReinforcePhase2(float dt);

    // シーン内の最初のボスを探す
    CCL::ECS::EntityID FindBoss() const;

    // ボスが第2形態に入っているか
    bool IsBossInPhase2() const;

    int  m_spawnCount         = 20;    // GUI の「生成」で生成する数
    bool m_autoSpawnDone      = false; // 初回配備が済んだか
    bool  m_reinforceRequested = false; // 増援のシグナルが届いた
    bool  m_reinforceStarted   = false; // 増援を出し始めたか（1回だけ）
    int   m_reinforceLeft      = 0;     // 増援のうち、まだ出していない数
    float m_reinforceTimer     = 0.0f;  // 次の一団を出すまでの残り [秒]
};
