#pragma once
// ===========================================================================
// 概要: BehaviorTreeComponent を持つエンティティの BT を毎フレーム評価する System。
//
// 責務:
//   - 起動時に行動・条件を登録し、辞書（BTActionIds.h）との突き合わせを行う
//   - 各個体へ木を配り、パスが変わったら実行状態を作り直す（直列）
//   - 木を評価する（並列）。割り込み・自己中断・打ち切りの後始末を含む
//   - アクションが申告した Read/Write の型をスケジューラへ渡す
//   - OnGui で blackboard の値と木を表示し、枝の強制実行と一時停止をできるようにする
//
// 備考: Action/Condition は BTActionRegistry から ID で引く（switch は使わない）。
//       評価の処理はボス固有の Component を読まない。ボスを知っているのは、
//       Initialize の登録関数の呼び出しと、デバッグ UI での要求の後始末だけ。
//
//       実装は役割ごとに分けてある（同じクラスのメンバ関数を別 .cpp で定義）:
//         BehaviorTreeSystem.cpp           初期化・木の配布・Update（毎フレームの入口）
//         BehaviorTreeSystem_Evaluate.cpp  木の評価（並列）
//         BehaviorTreeSystem_Debug.cpp     OnGui・強制実行のデバッグ UI
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

    // 型の申告に、アクションが GetComponent<T>() で触る型（レジストリが集めたもの）を足す
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override; // GetReadTypes の Write 版

    // アニメのトリガーを引くのは BT なので、それを読む AnimControllerSystem より前に走る（あちらは Engine なので、こちらに書く）
    std::vector<const char *> GetRunBefore() const override { return {"AnimControllerSystem"}; }

private:
    // 各エンティティへ、パスに対応する木を配り直す。並列ループの前に呼ぶ
    void SyncAssets();

    // 辞書に載っている行動・条件が、すべて実装として登録されているか調べる（起動時に1回）
    static void VerifyRegistrations();

    // --- 以下の評価系の関数は、木の評価からワーカースレッドで並列に呼ばれる ---
    //     引数は共通：nodeId=対象のノード、btComp=その個体の実行状態、myTrans=その個体の姿勢、
    //     asset=木、entity=その個体、dt=個体の経過時間（秒）

    // 木を再帰的に評価し、そのノードの結果を返す。ノードの種類ごとに下の Evaluate〜 へ振り分ける
    BTNodeState EvaluateTreeRecursive(
        BTNodeID                  nodeId,
        BehaviorTreeComponent    &btComp,
        const TransformComponent &myTrans,
        const BTAsset            &asset,
        CCL::ECS::EntityID        entity,
        float                     dt);

    // 先頭から順に試し、最初に失敗しなかった子の結果を返す。割り込み・自己中断もここで扱う
    BTNodeState EvaluateSelector(BTNodeID                  nodeId,
                                 BehaviorTreeComponent    &btComp,
                                 const TransformComponent &myTrans,
                                 const BTAsset            &asset,
                                 CCL::ECS::EntityID        entity,
                                 float                     dt);

    // 子を順に実行し、1つでも失敗したら Failure
    BTNodeState EvaluateSequence(BTNodeID                  nodeId,
                                 BehaviorTreeComponent    &btComp,
                                 const TransformComponent &myTrans,
                                 const BTAsset            &asset,
                                 CCL::ECS::EntityID        entity,
                                 float                     dt);

    // 子1つの結果を修飾する（反転・クールダウン・再試行）
    BTNodeState EvaluateDecorator(BTNodeID                  nodeId,
                                  BehaviorTreeComponent    &btComp,
                                  const TransformComponent &myTrans,
                                  const BTAsset            &asset,
                                  CCL::ECS::EntityID        entity,
                                  float                     dt);

    // 0番目の子（主）が走っている間だけ、残りの子（従）も動かす。結果は主のもの
    BTNodeState EvaluateParallel(BTNodeID                  nodeId,
                                 BehaviorTreeComponent    &btComp,
                                 const TransformComponent &myTrans,
                                 const BTAsset            &asset,
                                 CCL::ECS::EntityID        entity,
                                 float                     dt);

    // 子を weight に比例して1つ引く。失敗したら外して引き直す
    BTNodeState EvaluateWeightedSelector(BTNodeID                  nodeId,
                                         BehaviorTreeComponent    &btComp,
                                         const TransformComponent &myTrans,
                                         const BTAsset            &asset,
                                         CCL::ECS::EntityID        entity,
                                         float                     dt);

    // 今出せる子だけに絞り、点数の高い上位3つから点数に比例して1つ引く。自己中断の印も扱う
    BTNodeState EvaluateUtilitySelector(BTNodeID                  nodeId,
                                        BehaviorTreeComponent    &btComp,
                                        const TransformComponent &myTrans,
                                        const BTAsset            &asset,
                                        CCL::ECS::EntityID        entity,
                                        float                     dt);

    // UtilitySelector の子1つの点数（優先度 × 距離 × 状況による倍率 × 繰り返しの減点）。内訳を outDebug に書く
    float ScoreUtilityChild(BTNodeID                  childId,
                            BehaviorTreeComponent    &btComp,
                            const TransformComponent &myTrans,
                            const BTAsset            &asset,
                            CCL::ECS::EntityID        entity,
                            float                     dt,
                            BTScoreDebug             &outDebug);

    // アクション・条件へ渡す BTContext を作る。param はそのノードの数値パラメータ
    CCL::AI::BT::BTContext MakeContext(BTNodeID                  nodeId,
                                       BehaviorTreeComponent    &btComp,
                                       const TransformComponent &myTrans,
                                       CCL::ECS::EntityID        entity,
                                       float                     dt,
                                       float                     param);

    // 条件ノードを1つ評価する。未登録の条件は false
    bool EvaluateCondition(BTNodeID                  nodeId,
                           BehaviorTreeComponent    &btComp,
                           const TransformComponent &myTrans,
                           const BTAsset            &asset,
                           CCL::ECS::EntityID        entity,
                           float                     dt);

    // 枝の先頭に並ぶ条件だけを評価する（行動は実行しない）。先頭に条件が無い枝は false
    bool GuardsPass(BTNodeID                  nodeId,
                    BehaviorTreeComponent    &btComp,
                    const TransformComponent &myTrans,
                    const BTAsset            &asset,
                    CCL::ECS::EntityID        entity,
                    float                     dt);

    // UtilitySelector の候補に入れてよいか。条件の無い枝は、クールダウン中でなければ通す
    bool CandidatePasses(BTNodeID                  nodeId,
                         BehaviorTreeComponent    &btComp,
                         const TransformComponent &myTrans,
                         const BTAsset            &asset,
                         CCL::ECS::EntityID        entity,
                         float                     dt);

    // 枝の上に重なった Decorator のどれかがクールダウン中か
    bool TopCooldownActive(BTNodeID nodeId, const BehaviorTreeComponent &btComp, const BTAsset &asset) const;

    // 走っている枝を打ち切ってよいか。配下の行動が1つでも拒否したら false。AbortSubtree の前に呼ぶ
    bool CanAbortSubtree(BTNodeID                  nodeId,
                         BehaviorTreeComponent    &btComp,
                         const TransformComponent &myTrans,
                         const BTAsset            &asset,
                         CCL::ECS::EntityID        entity,
                         float                     dt);

    // 枝を打ち切る。配下の行動に後始末をさせ、実行状態を捨てる
    void AbortSubtree(BTNodeID                  nodeId,
                      BehaviorTreeComponent    &btComp,
                      const TransformComponent &myTrans,
                      const BTAsset            &asset,
                      CCL::ECS::EntityID        entity,
                      float                     dt);

    // 強制実行中か。true の間、評価は条件・クールダウン・割り込み・自己中断の関門を素通しにする
    static bool IgnoreGatesForDebug(const BehaviorTreeComponent &btComp);

    // 評価を始めるノードを返す（強制実行中はその枝、無ければルート）。評価の並列ループから呼ぶ
    BTNodeID ApplyDebugForcing(BehaviorTreeComponent &btComp, size_t nodeCount);

    // UtilitySelector が技を選んだ瞬間の内訳を btComp.debugPickLog に写す。topIds は最初の抽選の上位（点数の高い順）。
    // 上位が3つに足りなければ、空いた枠に候補外の子（点数の高い順）を入れる
    static void RecordUtilityPick(const BTAsset         &asset,
                                  BTNodeID               selectorId,
                                  BTNodeID               pickedId,
                                  const BTNodeID        *topIds,
                                  int                    topCount,
                                  BehaviorTreeComponent &btComp);

    // 枝の上に重なった Cooldown の残り時間 [秒]。クールダウン中でなければ 0（デバッグ表示用）
    static float CooldownLeft(BTNodeID nodeId, const BehaviorTreeComponent &btComp, const BTAsset &asset);

    // デバッグ UI。木を一覧にし、選んだ枝だけを評価させる（木のデータは書き換えない）
    void DrawForcedNodeGui(BehaviorTreeComponent &btComp, CCL::ECS::EntityID entity);

    // DrawForcedNodeGui の再帰部分。1ノードを1行で描き、実行ボタンを付ける。
    // parentIsUtility が true なら、スコア選択の子として点数も出す
    void DrawNodeRowRecursive(const BTAsset         &asset,
                              BTNodeID               nodeId,
                              BehaviorTreeComponent &btComp,
                              CCL::ECS::EntityID     entity,
                              bool                   parentIsUtility = false);
};
