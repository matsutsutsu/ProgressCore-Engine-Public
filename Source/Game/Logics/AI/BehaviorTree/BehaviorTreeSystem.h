// 汎用の BehaviorTree 評価エンジン
//
// 役割
//   BehaviorTreeComponent を持つ「どんなエンティティ」でも評価する。
//   ボス固有の概念（BossCommand/BossAction）を一切知らない。
//   Action/Condition は BTActionRegistry から引く（switch廃止）。
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

    // ------------------------------------------------------------------
    // スケジューラへの申告
    // ------------------------------------------------------------------
    // BTのアクションは必要なコンポーネントを ctx.world->GetComponent<T>() で
    // 自分で取りに行く（BTContext を汎用に保つため）。その書き込みは下の
    // Write<> テンプレート引数には現れないので、放っておくと TaskScheduler は
    // 「BehaviorTreeComponent しか触らない」と誤解し、実際には同じコンポーネントを
    // 書く別システムと同じバッチ（＝並列）に入れてしまう。
    //
    // BTActionRegistry が登録時に集めた申告をここで足して返すことで、
    // 「BTエンジンはボスもドローンも知らない」を保ったまま依存解析を正しくする。
    // 敵を追加してもアクション側が申告していれば自動で追従する。
    //
    // @note Initialize()（＝レジストリ登録）はシステム登録時に走り、
    //       BuildGraph は最初の UpdateLogic まで遅延されるので、この順序で成立する。
    std::vector<CCL::ECS::TypeID> GetReadTypes()  const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // アニメのトリガーを引くのはBTなので、消費側より前に走る必要がある。
    //
    // AnimControllerSystem 側に「BehaviorTreeSystem の後」と書かせない理由:
    //   あちらはエンジン、こちらはゲーム。エンジンがゲームのシステム名を知るのは逆流。
    std::vector<const char *> GetRunBefore() const override { return {"AnimControllerSystem"}; }

  private:
    // 各エンティティに「パスに対応する木」を配り直す（直列）。
    //
    // 読み込みそのものは BTAssetCache が持つ。ここは配布と、
    // 木が入れ替わったときの実行状態の破棄だけを行う。
    // 並列ループの手前で呼ぶ前提（ファイルI/Oをワーカースレッドへ持ち込まない）。
    void SyncAssets();

    /// 辞書（ActionRegistry.h）に載っている行動・条件がすべて実装登録されているか調べる。
    /// @note 手書きの辞書と実行時登録がずれると「エディタで選べるのに何も起きない技」になる。
    ///       ずれても動いてしまうので、起動時に一度だけ声を上げる。
    static void VerifyRegistrations();

    // 木を再帰的に評価する汎用エンジン。
    //        Action/Condition は registry から引いて実行する。
    BTNodeState EvaluateTreeRecursive(
        BTNodeID                  nodeId,
        BehaviorTreeComponent    &btComp,
        const TransformComponent &myTrans,
        const BTAsset            &asset,
        CCL::ECS::EntityID        entity,
        float                     dt);

    // デバッグUI：木を一覧にして「この枝を今すぐ実行」を選ばせる。
    //
    // 技を出す条件（距離・抽選）を待たずに狙った攻撃だけを確認するためのもの。
    // 木のデータは書き換えず、評価の開始ノードを差し替えるだけ。
    void DrawForcedNodeGui(BehaviorTreeComponent &btComp, CCL::ECS::EntityID entity);

    // 上の再帰部分。1ノード=1行を描き、実行ボタンを添える。
    void DrawNodeRowRecursive(const BTAsset         &asset,
                              BTNodeID               nodeId,
                              BehaviorTreeComponent &btComp,
                              CCL::ECS::EntityID     entity);
};
