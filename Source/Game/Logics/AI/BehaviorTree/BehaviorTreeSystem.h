// 汎用の BehaviorTree 評価エンジン。
//   BehaviorTreeComponent を持つどんなエンティティでも評価し、
//   ボス固有の概念（BossCommand/BossAction）を一切知らない。
//   Action/Condition は BTActionRegistry から引く（switch は使わない）。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"

class BehaviorTreeSystem : public CCL::ECS::IfSystem<
                               BehaviorTreeSystem,
                               CCL::ECS::Write<BehaviorTreeComponent>,
                               CCL::ECS::Read<TransformComponent>,
                               CCL::ECS::Read<TimeStateComponent>>
{
public:
    /// 申告の集約で基底の実装を呼ぶために別名を付ける（テンプレート引数の再掲を避ける）。
    using Base = CCL::ECS::IfSystem<BehaviorTreeSystem,
                                    CCL::ECS::Write<BehaviorTreeComponent>,
                                    CCL::ECS::Read<TransformComponent>,
                                    CCL::ECS::Read<TimeStateComponent>>;

    BehaviorTreeSystem();
    virtual ~BehaviorTreeSystem() override = default;

    void Initialize() override; // BTActions/BTConditions の登録
    void Update(float dt) override;

    void OnGui() override;

    // スケジューラへの申告。
    //   アクションが GetComponent<T>() で行う読み書きはテンプレート引数に現れず、
    //   放っておくとスケジューラが「BehaviorTreeComponent しか触らない」と誤解して、
    //   同じものを書く別システムと並列に走らせてしまう。
    //   レジストリが集めた申告をここで足すことで、「BTエンジンは敵の種類を知らない」を
    //   保ったまま依存解析を正しくする。
    //
    //   ただし効くのは同じステージ（優先度）にいる相手だけで、ステージが違えば型に
    //   関係なく直列になる。今 L02_Update で AnimParametersComponent を書くのは
    //   AnimControllerSystem だけなので、その型に限れば下の GetRunBefore と重なる。
    //   それでも型で申告するのは、順序宣言が名前で1対1にしか効かないのに対し、型は
    //   「それを触る全員」に効くから。後から同じステージへシステムを足して前後の宣言を
    //   忘れても、型が競合していれば直列化され、順序未定として起動時のログにも上がる。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // アニメのトリガーを引くのはBTなので、消費側より前に走る必要がある。
    //   逆向きに書かない（あちらはエンジン、こちらはゲーム）。
    std::vector<const char *> GetRunBefore() const override { return {"AnimControllerSystem"}; }

private:
    /// 各エンティティに「パスに対応する木」を配り直す（直列）。読み込みは BTAssetCache
    /// が持ち、ここは配布と実行状態の破棄だけ。並列ループの手前で呼ぶ。
    void SyncAssets();

    /// 辞書（ActionRegistry.h）に載っている行動・条件がすべて実装登録されているか調べる。
    /// @note 手書きの辞書と実行時登録がずれると「エディタで選べるのに何も起きない技」になる。
    ///       ずれても動いてしまうので、起動時に一度だけ声を上げる。
    static void VerifyRegistrations();

    /// 木を再帰的に評価する。Action/Condition は registry から引いて実行する。
    BTNodeState EvaluateTreeRecursive(
        BTNodeID                  nodeId,
        BehaviorTreeComponent    &btComp,
        const TransformComponent &myTrans,
        const BTAsset            &asset,
        CCL::ECS::EntityID        entity,
        float                     dt);

    /// アクション・条件へ渡す情報を組み立てる。
    CCL::AI::BT::BTContext MakeContext(BTNodeID                  nodeId,
                                       BehaviorTreeComponent    &btComp,
                                       const TransformComponent &myTrans,
                                       CCL::ECS::EntityID        entity,
                                       float                     dt,
                                       float                     param);

    /// 条件ノードを1つ評価する。木の評価と、割り込みの見張りの両方から呼ぶ。
    bool EvaluateCondition(BTNodeID                  nodeId,
                           BehaviorTreeComponent    &btComp,
                           const TransformComponent &myTrans,
                           const BTAsset            &asset,
                           CCL::ECS::EntityID        entity,
                           float                     dt);

    /// 割り込みたい枝の「見張り条件」だけを評価する。行動は実行しない。
    /// @note 行動を評価すると、アニメのトリガーを引き要求を書く＝出してしまう。
    ///       「今なら通るか」を副作用なしに知るために、条件だけを辿る専用の経路がいる。
    bool GuardsPass(BTNodeID                  nodeId,
                    BehaviorTreeComponent    &btComp,
                    const TransformComponent &myTrans,
                    const BTAsset            &asset,
                    CCL::ECS::EntityID        entity,
                    float                     dt);

    /// 走っている枝を打ち切ってよいか。配下の行動が1つでも拒否したら false。
    /// @note 打ち切りは途中でやめられないので、畳む前に全員へ聞く2段構えにしてある。
    bool CanAbortSubtree(BTNodeID                  nodeId,
                         BehaviorTreeComponent    &btComp,
                         const TransformComponent &myTrans,
                         const BTAsset            &asset,
                         CCL::ECS::EntityID        entity,
                         float                     dt);

    /// 枝を打ち切る。配下の行動に後始末をさせ、実行状態を畳む。
    void AbortSubtree(BTNodeID                  nodeId,
                      BehaviorTreeComponent    &btComp,
                      const TransformComponent &myTrans,
                      const BTAsset            &asset,
                      CCL::ECS::EntityID        entity,
                      float                     dt);

    /// デバッグUI：木を一覧にして「この枝を今すぐ実行」を選ばせる。
    /// 木のデータは書き換えず、評価の開始ノードを差し替えるだけ。
    void DrawForcedNodeGui(BehaviorTreeComponent &btComp, CCL::ECS::EntityID entity);

    /// 上の再帰部分。1ノード=1行を描き、実行ボタンを添える。
    void DrawNodeRowRecursive(const BTAsset         &asset,
                              BTNodeID               nodeId,
                              BehaviorTreeComponent &btComp,
                              CCL::ECS::EntityID     entity);
};
