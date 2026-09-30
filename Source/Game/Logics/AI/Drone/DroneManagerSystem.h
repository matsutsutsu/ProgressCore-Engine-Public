#pragma once
// ===========================================================================
// 概要: ドローンの生成・計数・破壊を管理する System。
//
// 責務:
//   - シーン開始時に、ボスが揃い次第初期ドローンを1回だけ配備する
//   - 生存数を数えて BattleContext.droneAliveCount に書く
//   - 通し番号と小隊の割り当て（ReindexAllDrones）
//   - 生成・破壊・陣形テスト・調整値のデバッグ UI
//
// 備考:
//   陣形計算は DroneFormationSystem、個体の判断は DroneBrainSystem が担う。
//   droneAliveCount は「ドローン数 = ボスHP」という構想のための値だが、現時点で
//   読むのはデバッグ表示だけで、BT の条件にも他の System にも繋がっていない。
//   ドローンを減らす経路も無い（Drone_Guide.md §9）ので、値は常に初期配備数のまま。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Time/TimeState.h"

namespace CCL::ECS
{
    class World;
}

// DroneComponent が Write なのは、Update の初回配備（TryAutoSpawnInitial → SpawnDrones）が
// 生成した機の DroneComponent と、ReindexAllDrones で全機の通し番号・小隊を書き換えるため。
class DroneManagerSystem : public CCL::ECS::IfSystem<DroneManagerSystem,
                                                     CCL::ECS::Write<DroneComponent>>
{
public:
    DroneManagerSystem()
        : IfSystem("DroneManagerSystem")
    {
    }

    // 同じ L02_Update に居る他のドローン系との前後を宣言する。
    //   スポーン時に ReindexAllDrones() が localIndex / totalDrones / squadId /
    //   slotInSquad を書き、陣形はそれを読んで持ち場の座標を出す。順序が入れ替わると
    //   配備した最初のフレームだけ古い通し番号で円が組まれる。
    //   DroneCommandSystem との間に実データ依存は無いが、生成(構造変更)を挟むので
    //   毎回同じ順で走るよう固定しておく。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneCommandSystem"};
    }

    // 毎フレーム、生存ドローンを数えて BattleContext.droneAliveCount に書く。
    // 初期配備が済んでいなければ、先に TryAutoSpawnInitial で配備する。
    void Update(float dt) override;

    // ---- 外部（GUI やゲームロジック）から呼ぶ操作 ----

    // ドローンを count 機、ボスの5m上に生成し、実際に生成できた数を返す。
    // ボスが居なければ生成せず 0 を返す。
    int SpawnDrones(int count);

    // ドローンを先頭から count 機破壊し、実際に破壊した数を返す。
    int DestroyDrones(int count);

    void DestroyAll();

    // 現在の生存数を返す（ViewEach で数える）。
    int GetAliveCount() const;

    /**
     * @brief 全ドローンの localIndex / totalDrones を振り直し、無所属の機を小隊へ入れる。
     *
     * 撃破の後には呼ばない。番号を振り直すと生き残った全機の持ち場が同時に変わり、
     * 1機落ちるたびに残りの全機が円周上を移動して並び直すため、削ったことが見えなくなる。
     * 据え置けば落ちた機の位置が穴として残る（totalDrones が実数より多いのはこのため）。
     * 小隊の欠員は SquadUpdateSystem が毎フレーム数え直して詰める。
     */
    void ReindexAllDrones();

    /**
     * @brief 全ドローンの「持ち場に着いた扱い」を一括で切り替える（デバッグ用）。
     *
     * 判断層(DroneBrainSystem)が毎フレーム currentState を書き直すので、状態を
     * 外から直接代入しても次のフレームで消える。外から効かせられるのは、判断層が
     * 入力として読むこのラッチだけ。
     *
     * @param treatAsArrived true=到着済み扱い(Idleで留まる) / false=再接近させる
     */
    void SetAllDronesArrived(bool treatAsArrived);

private:
    // ボスが準備でき次第、初期ドローンを1回だけ配備する。シーン開始直後はボス未生成の
    // フレームがあるため、揃うまで毎フレーム再試行する。
    void TryAutoSpawnInitial();

    // シーン内の最初のボス（BossCommandComponent と TransformComponent を持つ）を探す。
    CCL::ECS::EntityID FindBoss() const;

    // 陣形テスト UI。BT を組まずに、選んだ陣形の「形成 → 撃つ → 戻す」を手で送る。
    //   一覧に出すのは FormationRegistry に実装がある陣形だけ。enum には実装の無い枠
    //   （データの空き枠・欠番）が含まれていて、それを選ぶと静止するだけになる。
    void DrawFormationTest();

    // ボスへ直接命令を入れる。BT が上書きするので、確認は BT を止めてから行う。
    void SetBossCommand(DroneFormationType formation, bool attack);

    // インスペクタに出す陣形名。データ陣形は枠番号ではなくアセットが名乗る名前を返す
    // （"[空き枠] 枠0" のままでは、どの技を選んでいるのか画面から分からない）。
    const char *FormationDisplayName(DroneFormationType type) const;

    int m_spawnCount = 20; // GUI の「Spawn Drones」で生成する数

    bool m_autoSpawnEnabled  = true;  // シーン開始時に初期ドローンを自動配備するか
    bool m_autoSpawnDone     = false; // 初回配備が完了したか（1回だけ実行するためのラッチ）
    int  m_initialSpawnCount = 120;   // 初期配備数。高度合成数を参考に120にした

    // 陣形テストで選んでいる陣形と、その1つ前。
    //   C++実装とデータ実装が同じ絵になるかは、交互に出して目で見比べるしかない。
    //   毎回コンボを開き直すと形が崩れる前に切り替えられないので、往復を1ボタンにする。
    DroneFormationType m_testFormation     = DroneFormationType::GuillotineSlam;
    DroneFormationType m_testPrevFormation = DroneFormationType::DataSlot00;

public:
    void OnGui() override; // 生成・破壊・陣形テスト・調整値のデバッグ UI
};
