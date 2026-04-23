#pragma once
#include "ECS/System/CCL_System.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeData.h"

/**
 * @file BossAISystem.h
 * @brief ボスAIの思考評価を実行するECSシステムの定義
 * * 役割：
 * BehaviorTreeComponent を持つ全Entityを対象に、毎フレームの意思決定を行う。
 * * 特徴：
 * IfSystem を継承し、Read/Write 権限を明示することで
 * TaskScheduler による自動並列化（マルチスレッド）に対応している。
 */

 // IfSystemのテンプレート引数で要求コンポーネントを厳密に定義
class BossAISystem : public CCL::ECS::IfSystem<
    BossAISystem,
    CCL::ECS::Write<BehaviorTreeComponent>,
    CCL::ECS::Write<BossCommandComponent>
>
{
public:
    BossAISystem();
    virtual ~BossAISystem() override = default;

    virtual void Initialize() override;
    virtual void Update(float dt) override;

private:

    /**
    * @brief ビヘイビアツリーのフラット配列を非再帰（Iterative）で走査・評価する
    * * @param rootId  評価を開始するルートノードの配列インデックス
    * @param btComp  現在のエンティティが持つAI状態（Blackboardや実行中ノード情報）
    * @param cmdComp 評価結果（行動要求）を書き込むための出力用コンポーネント
    * @param asset   全エンティティで共有される、Bake済みのBT静的データ配列
    * @return CCL::AI::BTNodeState 評価の最終結果（Success, Failure, Running）
    * * @note 【アーキテクチャ上の注意点】
    * この関数は再帰呼び出し（Recursive Call）を意図的に排除している。
    * 代わりに固定長配列（nodeStack）を用いたスタックマシンとして動作する。
    * * @warning 【パフォーマンスと安全性】
    * ヒープアロケーション（new/std::vector）は絶対に使用しない。
    * MAX_STACK_DEPTH は設計上の最大ツリー深度を表す。これを超える深さの
    * アセットが渡された場合の保護機構は最小限にとどめ、最内ループの速度を最優先している。
    */
    CCL::AI::BTNodeState EvaluateTreeIterative(
        CCL::AI::BTNodeID rootId,
        BehaviorTreeComponent& btComp,
        BossCommandComponent& cmdComp,
        const CCL::AI::BTAsset& asset
    );
    bool EvaluateCondition(CCL::AI::ActionID condId, const BehaviorTreeComponent& btComp);
    CCL::AI::BTNodeState ExecuteAction(CCL::AI::ActionID actionId, BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp);

    // （仮）メモリ上に保持するボスAIアセット。
    // ※本来は _world->GetResource<ResourceManager> 等で取得すべき共有データです
    CCL::AI::BTAsset m_bossSharedAsset;
};