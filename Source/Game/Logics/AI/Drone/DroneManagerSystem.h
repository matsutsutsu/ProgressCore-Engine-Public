// ドローンの「生死・数」を管理する司令塔システム。生成・計数・破壊だけを持ち、
// 陣形計算は DroneFormationSystem、個体の判断は DroneBrainSystem が担う。
//
// 生存数を BattleContext.droneAliveCount に書く。
//   「ドローン数 = ボスHP」という構想の受け皿だが、現時点では読み手がデバッグ表示
//   だけで、BT の条件にも他のシステムにも繋がっていない。加えてドローンを減らす
//   経路そのものが無い（Drone_Guide.md §9）ので、この値は常に初期配備数のまま。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Time/TimeState.h"

namespace CCL::ECS
{
class World;
}

class DroneManagerSystem : public CCL::ECS::IfSystem<DroneManagerSystem,
      CCL::ECS::Read<DroneComponent>> // 数えるだけなので Read
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

    /// 毎フレーム、生存ドローンを数えて BattleContext.droneAliveCount に書く。
    void Update(float dt) override;

    // ---- 外部（GUI やゲームロジック）から呼ぶ操作 ----

    /// ドローンを count 機スポーンし、実際に生成できた数を返す。
    /// basePos は生成中心（通常はボス位置）。nullptr ならワールド原点付近。
    int SpawnDrones(int count);

    /// ドローンを count 機破壊し、実際に破壊した数を返す。
    int DestroyDrones(int count);

    /// 全ドローンを破壊する。
    void DestroyAll();

    /// 現在の生存数を返す（View で数える）。
    int GetAliveCount() const;
    
    /// 全ドローンに対して localIndex / totalDrones を振り直す。
    void ReindexAllDrones();

    /// 全ドローンの「持ち場に着いた扱い」を一括で切り替える（デバッグ用）。
    ///
    /// currentState を直接代入する版は廃止した。判断層(DroneBrainSystem)が
    /// 毎フレーム全経路で currentState を書き直すため、外から入れた値は次の
    /// フレームに必ず消える＝押しても何も起きないボタンになっていた。
    /// 実際に効くのは、判断層が入力として読むこのラッチだけ。
    /// @param treatAsArrived true=到着済み扱い(Idleで留まる) / false=再接近させる
    void SetAllDronesArrived(bool treatAsArrived);

  private:
    /// ボスが準備でき次第、初期ドローンを1回だけ配備する。シーン開始直後はボス未生成の
    /// フレームがあるため、揃うまで毎フレーム再試行する。
    void TryAutoSpawnInitial();

    /// シーン内の最初のボス（BossCommandComponent 保持）を探す。
    CCL::ECS::EntityID FindBoss() const;

    /// 陣形テストUI。BT を組まずに、選んだ陣形の「形成 → 撃つ → 戻す」を手で送る。
    void DrawFormationTest();

    /// ボスへ直接命令を入れる。BT が上書きするので、確認は BT を止めてから行う。
    void SetBossCommand(DroneFormationType formation, bool attack);

    /// インスペクタに出す陣形名。データ陣形は枠番号ではなくアセットが名乗る名前を返す
    /// （"[データ] 枠0" のままでは、どの技を選んでいるのか画面から分からない）。
    const char *FormationDisplayName(DroneFormationType type) const;

    // GUI 用パラメータ。
    int   m_spawnCount  = 12;
    float m_spawnRadius = 8.0f;
    float m_spawnSpeed  = 40.0f;

    bool m_autoSpawnEnabled = true;  // シーン開始時に初期ドローンを自動配備するか
    bool m_autoSpawnDone    = false; // 初回配備が完了したか（1回だけ実行するためのラッチ）
    int  m_initialSpawnCount = 100;  // 初期配備数（展示会向け。スペックに応じて調整可）

    // 陣形テストで選んでいる陣形と、その1つ前。
    //   C++実装とデータ実装が同じ絵になるかは、交互に出して目で見比べるしかない。
    //   毎回コンボを開き直すと形が崩れる前に切り替えられないので、往復を1ボタンにする。
    DroneFormationType m_testFormation     = DroneFormationType::GuillotineSlam;
    DroneFormationType m_testPrevFormation = DroneFormationType::DataSlot00;

  public:
    void OnGui() override; // 生成/破壊のデバッグUI
};