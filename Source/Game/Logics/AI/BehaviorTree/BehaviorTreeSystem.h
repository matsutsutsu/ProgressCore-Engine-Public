#pragma once
// ===========================================================================
// 概要: BehaviorTreeComponent を持つエンティティの BT を毎フレーム評価する System。
//
// 責務:
//   - 起動時に行動・条件を登録し、辞書（ActionRegistry.h）との突き合わせを行う
//   - 各個体へ木を配り、パスが変わったら実行状態を作り直す（直列）
//   - 木を評価する（並列）。割り込み・自己中断・打ち切りの後始末を含む
//   - アクションが申告した Read/Write の型をスケジューラへ渡す
//   - OnGui で blackboard の値と木を表示し、枝の強制実行と一時停止をできるようにする
//
// 備考: Action/Condition は BTActionRegistry から ID で引く（switch は使わない）。
//       評価の処理はボス固有の Component を読まない。ボスを知っているのは、
//       Initialize の登録関数の呼び出しと、デバッグ UI での要求の後始末だけ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"

class BehaviorTreeSystem : public CCL::ECS::IfSystem<
                               BehaviorTreeSystem,
                               CCL::ECS::Write<BehaviorTreeComponent>,
                               CCL::ECS::Read<TransformComponent>,
                               CCL::ECS::Read<TimeStateComponent>>
{
public:
    // 申告をまとめるときに基底の実装を呼ぶための別名（テンプレート引数を何度も書かないため）。
    using Base = CCL::ECS::IfSystem<BehaviorTreeSystem,
                                    CCL::ECS::Write<BehaviorTreeComponent>,
                                    CCL::ECS::Read<TransformComponent>,
                                    CCL::ECS::Read<TimeStateComponent>>;

    BehaviorTreeSystem() : IfSystem("BehaviorTreeSystem") {}
    virtual ~BehaviorTreeSystem() override = default;

    void Initialize() override; // 行動・条件の登録と、辞書との突き合わせ
    void Update(float dt) override;

    void OnGui() override;

    /**
     * @brief アクションが読み書きする型をスケジューラへ申告する（GetWriteTypes も同じ）。
     *
     * アクションが GetComponent<T>() で行う読み書きはテンプレート引数に現れない。
     * そのままだとスケジューラが「BehaviorTreeComponent しか触らない」と判断して、同じものを書く別の System と並列に走らせてしまう。
     * レジストリが集めた申告をここで足すことで、この System が敵の種類を知らないまま、依存の解析を正しくする。
     *
     * ただし効くのは同じステージ（優先度）にいる相手だけで、ステージが違えば型に関係なく直列になる。
     * 今 L02_Update で AnimParametersComponent を書くのは AnimControllerSystem だけなので、その型に限れば下の GetRunBefore と重なる。
     *
     * それでも型で申告するのは、順序の宣言が名前で1対1にしか効かないのに対し、型はそれを触る全員に効くから。
     * 後から同じステージへ System を足して前後の宣言を忘れても、型が競合していれば直列になり、順序が決まっていないと起動時のログにも出る。
     */
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // アニメのトリガーを引くのは BT なので、それを読む AnimControllerSystem より前に走る。
    //   逆向き（あちらに GetRunAfter）には書かない（あちらは Engine、こちらは Game なので）。
    std::vector<const char *> GetRunBefore() const override { return {"AnimControllerSystem"}; }

private:
    /**
     * @brief 各エンティティにパスに対応する木を配り直す（直列）。
     *
     * 読み込みは BTAssetCache が行い、ここは配ることと実行状態を作り直すことだけ。
     *
     * @note 並列ループの前に呼ぶ。
     */
    void SyncAssets();

    /**
     * @brief 辞書（ActionRegistry.h）に載っている行動・条件が、すべて実装として登録されているか調べる。
     * @note 辞書と実行時の登録がずれると「エディタで選べるのに何も起きない技」になる。
     *       ずれても動いてしまうので、起動時に一度だけエラーログを出す。
     */
    static void VerifyRegistrations();

    // 木を再帰的に評価する。Action/Condition はレジストリから引いて実行する。
    BTNodeState EvaluateTreeRecursive(
        BTNodeID                  nodeId,
        BehaviorTreeComponent    &btComp,
        const TransformComponent &myTrans,
        const BTAsset            &asset,
        CCL::ECS::EntityID        entity,
        float                     dt);

    // アクション・条件へ渡す BTContext を作る。
    CCL::AI::BT::BTContext MakeContext(BTNodeID                  nodeId,
                                       BehaviorTreeComponent    &btComp,
                                       const TransformComponent &myTrans,
                                       CCL::ECS::EntityID        entity,
                                       float                     dt,
                                       float                     param);

    // 条件ノードを1つ評価する。木の評価と、割り込み・自己中断の判定の両方から呼ぶ。
    bool EvaluateCondition(BTNodeID                  nodeId,
                           BehaviorTreeComponent    &btComp,
                           const TransformComponent &myTrans,
                           const BTAsset            &asset,
                           CCL::ECS::EntityID        entity,
                           float                     dt);

    /**
     * @brief 枝の先頭に並ぶ条件だけを評価する。行動は実行しない。
     * @note 行動を評価すると、アニメのトリガーを引いて要求を書く（技を出してしまう）。
     *       今なら通るかを副作用なしに知るために、条件だけをたどる専用の経路がいる。
     */
    bool GuardsPass(BTNodeID                  nodeId,
                    BehaviorTreeComponent    &btComp,
                    const TransformComponent &myTrans,
                    const BTAsset            &asset,
                    CCL::ECS::EntityID        entity,
                    float                     dt);

    /**
     * @brief 走っている枝を打ち切ってよいか。配下の行動が1つでも拒否したら false。
     * @note 打ち切りは途中でやめられないので、打ち切る前に全員に確認する2段階にしている。
     */
    bool CanAbortSubtree(BTNodeID                  nodeId,
                         BehaviorTreeComponent    &btComp,
                         const TransformComponent &myTrans,
                         const BTAsset            &asset,
                         CCL::ECS::EntityID        entity,
                         float                     dt);

    // 枝を打ち切る。配下の行動に後始末をさせ、実行状態を捨てる。
    void AbortSubtree(BTNodeID                  nodeId,
                      BehaviorTreeComponent    &btComp,
                      const TransformComponent &myTrans,
                      const BTAsset            &asset,
                      CCL::ECS::EntityID        entity,
                      float                     dt);

    /**
     * @brief デバッグ UI。木を一覧にして、この枝を今すぐ実行するかを選べるようにする。
     *
     * 木のデータは書き換えず、評価を始めるノードを差し替えるだけ。
     *
     * @param btComp 表示・操作する BT。
     * @param entity BT を持つエンティティ。
     */
    void DrawForcedNodeGui(BehaviorTreeComponent &btComp, CCL::ECS::EntityID entity);

    // DrawForcedNodeGui の再帰部分。1ノードを1行で描き、実行ボタンを付ける。
    void DrawNodeRowRecursive(const BTAsset         &asset,
                              BTNodeID               nodeId,
                              BehaviorTreeComponent &btComp,
                              CCL::ECS::EntityID     entity);
};
