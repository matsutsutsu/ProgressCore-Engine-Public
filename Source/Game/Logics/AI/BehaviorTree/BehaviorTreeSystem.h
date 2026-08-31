/**
 * 汎用の BehaviorTree 評価エンジン。BossAISystem から抽出した。
 *
 * 役割
 *   BehaviorTreeComponent を持つ「どんなエンティティ」でも評価する。
 *   ボス固有の概念（BossCommand/BossAction）を一切知らない。
 *   Action/Condition は BTActionRegistry から引く（switch廃止）。
 *
 * ボス固有処理はどこへ行ったか
 *   「移動してほしい」「この陣形にしてほしい」等の出力は、
 *   各 IBTAction（BTActions_Boss.h）が world から BossCommandComponent を
 *   自分で取得して書き込む。エンジンは関与しない。
 *
 *   唯一の例外がデバッグUI（OnGui）で、枝の強制実行を解除したときに
 *   ボスの「今撃て」信号を下ろしている。これは ENABLE_EDITOR の中だけに
 *   閉じてあるので、製品ビルドのエンジンはボスを一切知らない。
 *
 * フィルタ
 *   Write<BehaviorTreeComponent>, Read<TransformComponent>, Read<TimeStateComponent>
 *   のみ。HealthComponent や BossActionComponent への依存を外したので、
 *   それらを持たないエンティティ（将来のドローン等）でも動く。
 */
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
    BehaviorTreeSystem();
    virtual ~BehaviorTreeSystem() override = default;

    void Initialize() override; // BTActions/BTConditions の登録
    void Update(float dt) override;

    void OnGui() override;

  private:
    /// 各エンティティに「パスに対応する木」を配り直す（直列）。
    /// @note  読み込みそのものは BTAssetCache が持つ。ここは配布と、
    ///        木が入れ替わったときの実行状態の破棄だけを行う。
    ///        並列ループの手前で呼ぶ前提（ファイルI/Oをワーカースレッドへ持ち込まない）。
    void SyncAssets();

    /// 木を再帰的に評価する汎用エンジン。
    ///        Action/Condition は registry から引いて実行する。
    BTNodeState EvaluateTreeRecursive(
        BTNodeID                  nodeId,
        BehaviorTreeComponent    &btComp,
        const TransformComponent &myTrans,
        const BTAsset            &asset,
        CCL::ECS::EntityID        entity,
        float                     dt);

#ifdef ENABLE_EDITOR
    /// デバッグUI：木を一覧にして「この枝を今すぐ実行」を選ばせる。
    /// @note  技を出す条件（距離・抽選）を待たずに狙った攻撃だけを確認するためのもの。
    ///        木のデータは書き換えず、評価の開始ノードを差し替えるだけ。
    void DrawForcedNodeGui(BehaviorTreeComponent &btComp, CCL::ECS::EntityID entity);

    /// 上の再帰部分。1ノード=1行を描き、実行ボタンを添える。
    void DrawNodeRowRecursive(const BTAsset         &asset,
                              BTNodeID               nodeId,
                              BehaviorTreeComponent &btComp,
                              CCL::ECS::EntityID     entity);
#endif
};