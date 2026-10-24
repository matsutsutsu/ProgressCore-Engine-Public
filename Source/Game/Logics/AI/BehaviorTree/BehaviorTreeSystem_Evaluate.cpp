#include "BehaviorTreeSystem.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/BTUtilityScore.h"

#include "Engine/Platform/Core/Logger.h"

#include <algorithm>

using namespace CCL::ECS;
using namespace CCL::AI;
using namespace CCL::AI::BT;

// ---------------------------------------------------------------------------
// 乱数・抽選と実行状態の後始末
// ---------------------------------------------------------------------------

namespace
{
    /**
     * @brief xorshift32 で次の乱数を返す。質は要らないが再現性は要る。
     * @param state 乱数の状態。BehaviorTreeComponent が持つので個体ごとに独立し、ワーカースレッドをまたいでも列が混ざらない。
     * @return 次の乱数。state も同じ値へ進む。
     */
    uint32_t NextRandom(uint32_t &state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // [0,1) の実数を1つ引く。上位24bitだけ使う（xorshift は下位bitの質が落ちる）。
    float NextRandom01(uint32_t &state)
    {
        return static_cast<float>(NextRandom(state) >> 8) * (1.0f / 16777216.0f);
    }

    // 子の並び（node.children）の中で childId が何番目か。見つからなければ -1
    int IndexOfChild(const BTNode &node, BTNodeID childId)
    {
        for (size_t i = 0; i < node.children.size(); ++i)
        {
            if (node.children[i] == childId)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // weights の先頭 count 個から、値に比例して1つ引いて添字を返す。total は count 個の合計
    int PickWeighted(const float *weights, int count, float total, uint32_t &rngState)
    {
        // 累積和を乱数が超えた所が当選
        float r = NextRandom01(rngState) * total;
        for (int i = 0; i < count; ++i)
        {
            r -= weights[i];
            if (r <= 0.0f)
            {
                return i;
            }
        }
        return count - 1; // 丸め誤差で最後まで超えなかったときの保険
    }

    /**
     * @brief サブツリーの実行状態（再開位置と行動の残り時間）をまとめて捨てる。
     *
     * ResetChildActionsRecursive との違いは、再開位置（runningNodes）も消すこと。
     * Parallel が「従」を止めるときに使う。
     * 従は次のフレームから評価されなくなるので、途中の再開位置と残り時間を残すと、次にこの枝へ来たときに前回の続きから再開してしまう。
     *
     * Cooldown のタイマーは消さない（Action だけ -1 に戻す）。
     * クールダウンは枝をまたいで持ち越すべき情報で、ここで消すとクールダウン中の技が出せてしまう。
     *
     * @param nodeId 捨てる枝の根のノード。
     * @param btComp 実行状態を持つコンポーネント。
     * @param asset  木の構造。
     */
    void ResetSubtreeStateRecursive(BTNodeID nodeId, BehaviorTreeComponent &btComp, const BTAsset &asset)
    {
        const auto &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action && nodeId < btComp.nodeTimers.size())
        {
            btComp.nodeTimers[nodeId] = -1.0f;
        }
        if (nodeId < btComp.runningNodes.size())
        {
            btComp.runningNodes[nodeId] = -1;
        }
        for (auto childId : node.children)
        {
            ResetSubtreeStateRecursive(childId, btComp, asset);
        }
    }

    // 枝の下にある Action ノードのタイマーだけを -1（未実行）に戻す。再開位置と Cooldown は残す
    void ResetChildActionsRecursive(BTNodeID nodeId, BehaviorTreeComponent &btComp, const BTAsset &asset)
    {
        const auto &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action)
        {
            btComp.nodeTimers[nodeId] = -1.0f;
        }
        for (auto childId : node.children)
        {
            ResetChildActionsRecursive(childId, btComp, asset);
        }
    }
}

// ---------------------------------------------------------------------------
// アクション・条件へ渡す情報
// ---------------------------------------------------------------------------

BTContext BehaviorTreeSystem::MakeContext(BTNodeID                  nodeId,
                                          BehaviorTreeComponent    &btComp,
                                          const TransformComponent &myTrans,
                                          EntityID                  entity,
                                          float                     dt,
                                          float                     param)
{
    BTContext ctx;
    ctx.world       = m_world;
    ctx.entity      = entity;
    ctx.bt          = &btComp; // blackboard と実行状態
    ctx.myTransform = &myTrans;
    ctx.nodeId      = nodeId;  // nodeTimers[nodeId] を引くのに使う
    ctx.dt          = dt;
    ctx.param       = param;
    return ctx;
}

// ---------------------------------------------------------------------------
// 木の評価
// ---------------------------------------------------------------------------

bool BehaviorTreeSystem::EvaluateCondition(BTNodeID                  nodeId,
                                           BehaviorTreeComponent    &btComp,
                                           const TransformComponent &myTrans,
                                           const BTAsset            &asset,
                                           EntityID                  entity,
                                           float                     dt)
{
    const BTNode &node = asset.nodes[nodeId];

    // 並列処理の中で Instance() を読むが、起動後は読み取りだけなので競合は起きない。
    IBTCondition *cond = BTActionRegistry::Instance().GetCondition(node.actionOrConditionId);
    if (!cond) return false;

    BTContext ctx = MakeContext(nodeId, btComp, myTrans, entity, dt, node.param);
    return cond->Evaluate(ctx);
}


// 先頭に並ぶ条件だけを評価する。条件が無い枝は false
bool BehaviorTreeSystem::GuardsPass(BTNodeID                  nodeId,
                                    BehaviorTreeComponent    &btComp,
                                    const TransformComponent &myTrans,
                                    const BTAsset            &asset,
                                    EntityID                  entity,
                                    float                     dt)
{
    // 割り込み・自己中断で「今なら通るか」を知るための経路。
    // 行動を評価すると技を出してしまうので、条件だけをたどる。

    // どんな形なら条件を先に調べられるかは HasReadableGuard が決める。ここは同じ形をたどって条件を評価する。
    // 条件が無い形を false にしているのは、無条件の割り込みを許さないため。
    if (!HasReadableGuard(asset, nodeId)) return false;

    const BTNode &node = asset.nodes[nodeId];
    switch (node.type)
    {
    case BTNodeType::Condition: {
        const bool ok = EvaluateCondition(nodeId, btComp, myTrans, asset, entity, dt);

        // 木の色分けにも残す。「印は付いているのに割り込まない」とき、どの条件で止まって
        // いるかがエディタ上で分かる。
        btComp.debugNodeStates[nodeId] = ok ? BTDebugState::Success : BTDebugState::Failure;
        return ok;
    }

    case BTNodeType::Decorator:
        switch (node.decoratorType)
        {
        // クールダウン中の枝へは割り込ませない。ここを見ないと、通常の評価ではクールダウン中で
        // 出せない技が、割り込みのときだけ出せてしまう。
        case BTDecoratorType::Cooldown:
            if (btComp.nodeTimers[nodeId] > 0.0f) return false;
            break;

        case BTDecoratorType::Inverter:
            return !GuardsPass(node.children[0], btComp, myTrans, asset, entity, dt);

        // Retry は同じフレームに繰り返すだけ。条件の結果は変わらないので素通しでよい。
        case BTDecoratorType::Retry: break;
        }
        return GuardsPass(node.children[0], btComp, myTrans, asset, entity, dt);

    // 先頭に並ぶ条件を全部見る。条件でない子（行動）が来た所で終わる。
    case BTNodeType::Sequence:
        for (const BTNodeID childId : node.children)
        {
            // 先頭に並ぶ条件を全部見る。条件でない子（行動）が来た所で終わる。
            if (!HasReadableGuard(asset, childId)) break;
            if (!GuardsPass(childId, btComp, myTrans, asset, entity, dt)) return false;
        }
        return true;

    default: return false;
    }
}

// UtilitySelector の候補に入れていいかどうか
bool BehaviorTreeSystem::CandidatePasses(BTNodeID                  nodeId,
                                         BehaviorTreeComponent    &btComp,
                                         const TransformComponent &myTrans,
                                         const BTAsset            &asset,
                                         EntityID                  entity,
                                         float                     dt)
{
    // 強制実行中は絞らない（関門の素通し。理由は IgnoreGatesForDebug）
    if (IgnoreGatesForDebug(btComp)) return true;

    // 先頭に条件がある枝は、その条件（と途中の Cooldown）で決める
    if (HasReadableGuard(asset, nodeId))
    {
        return GuardsPass(nodeId, btComp, myTrans, asset, entity, dt);
    }

    // 条件が無い枝（様子見など）は通す。ただしクールダウン中なら外す。外さないと上位の枠を占める。
    // GuardsPass を「条件が無ければ通る」に変えて済ませてはいけない（割り込みの挙動まで変わる）。
    return !TopCooldownActive(nodeId, btComp, asset);
}

bool BehaviorTreeSystem::TopCooldownActive(BTNodeID nodeId, const BehaviorTreeComponent &btComp, const BTAsset &asset) const
{
    // Cooldown の下に Inverter などが重なっていても、たどれる所まで降りて全部見る
    for (BTNodeID id = nodeId; id < asset.nodes.size();)
    {
        const BTNode &node = asset.nodes[id];
        if (node.type != BTNodeType::Decorator)
        {
            return false;
        }

        if (node.decoratorType == BTDecoratorType::Cooldown && btComp.nodeTimers[id] > 0.0f)
        {
            return true;
        }

        if (node.children.empty())
        {
            return false;
        }
        id = node.children[0];
    }
    return false;
}

bool BehaviorTreeSystem::CanAbortSubtree(BTNodeID                  nodeId,
                                         BehaviorTreeComponent    &btComp,
                                         const TransformComponent &myTrans,
                                         const BTAsset            &asset,
                                         EntityID                  entity,
                                         float                     dt)
{
    // 打ち切りは途中でやめられないので、打ち切る前に全員に確認する（AbortSubtree と2段階にしている）
    const BTNode &node = asset.nodes[nodeId];

    if (node.type == BTNodeType::Action)
    {
        if (IBTAction *action = BTActionRegistry::Instance().GetAction(node.actionOrConditionId))
        {
            BTContext ctx = MakeContext(nodeId, btComp, myTrans, entity, dt, node.param);
            if (!action->CanAbort(ctx)) return false;
        }
    }

    for (const BTNodeID childId : node.children)
        if (!CanAbortSubtree(childId, btComp, myTrans, asset, entity, dt)) return false;

    return true;
}

void BehaviorTreeSystem::AbortSubtree(BTNodeID                  nodeId,
                                      BehaviorTreeComponent    &btComp,
                                      const TransformComponent &myTrans,
                                      const BTAsset            &asset,
                                      EntityID                  entity,
                                      float                     dt)
{
    const BTNode &node = asset.nodes[nodeId];

    if (node.type == BTNodeType::Action)
    {
        // 走っていなかった行動にも OnAbort を呼ぶ。後始末はどれも「自分が出した要求なら取り下げる」
        // 形なので、出していない行動が呼ばれても何も起きない（ClearCommandIfMine）。
        // どれが走っていたかを正確に持つより、何度呼んでも同じ結果になる後始末の方が壊れにくい。
        if (IBTAction *action = BTActionRegistry::Instance().GetAction(node.actionOrConditionId))
        {
            BTContext ctx = MakeContext(nodeId, btComp, myTrans, entity, dt, node.param);
            action->OnAbort(ctx);
        }

        // 残り時間を「未実行」へ戻す。途中の値が残ると、次にこの枝へ来たとき開始フレームと
        // 判定されず、アニメのトリガーも引かれないまま残り時間だけ待って終わる。
        btComp.nodeTimers[nodeId] = -1.0f;
    }
    else if (node.type == BTNodeType::Decorator && node.decoratorType == BTDecoratorType::Cooldown)
    {
        // 打ち切りでもクールダウンは掛ける。技は出してしまっているので、掛けないと
        // 割り込みで中断されるたびに即また出せることになる。
        btComp.nodeTimers[nodeId] = node.param;
    }

    btComp.runningNodes[nodeId] = -1;

    for (const BTNodeID childId : node.children)
        AbortSubtree(childId, btComp, myTrans, asset, entity, dt);
}



// ---------------------------------------------------------------------------
// 木の評価（再帰）
// ---------------------------------------------------------------------------
BTNodeState BehaviorTreeSystem::EvaluateTreeRecursive(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    // ノードの種類ごとの評価へ振り分け、結果をデバッグ表示用に残す
    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    switch (node.type)
    {
    case BTNodeType::Selector:         result = EvaluateSelector(nodeId, btComp, myTrans, asset, entity, dt);         break;
    case BTNodeType::Sequence:         result = EvaluateSequence(nodeId, btComp, myTrans, asset, entity, dt);         break;
    case BTNodeType::Decorator:        result = EvaluateDecorator(nodeId, btComp, myTrans, asset, entity, dt);        break;
    case BTNodeType::Parallel:         result = EvaluateParallel(nodeId, btComp, myTrans, asset, entity, dt);         break;
    case BTNodeType::WeightedSelector: result = EvaluateWeightedSelector(nodeId, btComp, myTrans, asset, entity, dt); break;
    case BTNodeType::UtilitySelector:  result = EvaluateUtilitySelector(nodeId, btComp, myTrans, asset, entity, dt);  break;

    case BTNodeType::Condition: {
        // 強制実行中は無条件に通す
        if (IgnoreGatesForDebug(btComp))
        {
            result = BTNodeState::Success;
            break;
        }

        result = EvaluateCondition(nodeId, btComp, myTrans, asset, entity, dt) ? BTNodeState::Success
                                                                              : BTNodeState::Failure;
        break;
    }

    case BTNodeType::Action: {
        // 並列処理の中で Instance() を読むけど、起動後は読み取りだけなので競合は起きない
        IBTAction *action = BTActionRegistry::Instance().GetAction(node.actionOrConditionId);

        // アクションが無ければ失敗にする
        if (action)
        {
            BTContext ctx = MakeContext(nodeId, btComp, myTrans, entity, dt, node.param);
            result        = action->Execute(ctx);
        }
        else
        {
            result = BTNodeState::Failure;
        }

        // 終わった行動は自分のタイマーを「未実行(-1)」へ戻す。この処理を Sequence の
        // 中だけに置いてはいけない。Selector や Decorator の直下の行動が一度完了すると
        // 0 のままになり、以後ずっと即 Success（Wait が待たない／攻撃が二度と出ない）になる。
        if (result != BTNodeState::Running && nodeId < btComp.nodeTimers.size())
        {
            btComp.nodeTimers[nodeId] = -1.0f;
        }
        break;
    }
    }

    // BT エディタの色分け表示のために、評価結果を残す
    BTDebugState dbgState = BTDebugState::None;
    if (result == BTNodeState::Success)      dbgState = BTDebugState::Success;
    else if (result == BTNodeState::Failure) dbgState = BTDebugState::Failure;
    else if (result == BTNodeState::Running) dbgState = BTDebugState::Running;
    btComp.debugNodeStates[nodeId] = dbgState;

    // Running を返した最初のノードをデバッグ表示用に残す
    if (result == BTNodeState::Running && btComp.runningNodeId == 0xFFFF)
    {
        btComp.runningNodeId = nodeId;
    }
    return result;
}

// ---------------------------------------------------------------------------
// ノードの種類ごとの評価
// ---------------------------------------------------------------------------

BTNodeState BehaviorTreeSystem::EvaluateSelector(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    // 処理フロー:
    // 1. 割り込み：上の枝が走っている枝を打ち切る
    // 2. 自己中断：走っている枝が自分から降りる
    // 3. 再開：Running の子を続ける
    // 4. 選び直し：先頭から順に試す

    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    int runningIndex = btComp.runningNodes[nodeId];

    // ---------------------------------------------------------
    // 1. 割り込み
    // ---------------------------------------------------------

    // 走っている枝より上に interrupt の付いた枝があり、その先頭の条件が通るなら、
    // 走っている枝を打ち切ってそちらへ切り替える。

    // 印の付いた枝だけを見る。全部の枝を毎フレーム見直さないのは、再開位置（runningNodes）が
    // 技を続けるための仕組みだから。Running の子を先に再開するので、出している最中の技が
    // 次のフレームに別の技へ変わらない。割り込みはその例外を枝ごとに許す仕組みで、既定は許さない。

    // 強制実行中は見ない（関門の素通し。理由は IgnoreGatesForDebug）。

    // 割り込んだ枝がすぐ Failure になると、下の枝を毎フレーム打ち切っては
    // やり直すことになり、技が一度も最後まで出ない。先頭の条件は「通ったら本当にその枝が
    // 動く」ものにすること（被弾の枝が蓄積をリセットして自分で抜けるのが良い例）。
    if (runningIndex > 0 && runningIndex < (int)node.children.size() && !IgnoreGatesForDebug(btComp))
    {
        for (int i = 0; i < runningIndex; ++i)
        {
            if (!asset.nodes[node.children[i]].interrupt) continue;
            if (!GuardsPass(node.children[i], btComp, myTrans, asset, entity, dt)) continue;

            // 打ち切れない技が走っている間は見送る。次のフレームにまた見にくるので、
            // 空中の技なら着地した最初のフレームに入れ替わる。
            if (!CanAbortSubtree(node.children[runningIndex], btComp, myTrans, asset, entity, dt)) break;

            // 割り込みは画面からは読み取りにくい変化なので、ログに残す。
            // 毎フレーム出るなら、割り込んだ枝がすぐ失敗して切り替えを繰り返している（上の注意）。
            CCL_LOG_INFO(LogCategory::AI, "[BT] 割り込み: %d 番目の枝が %d 番目の枝を打ち切った (nodes[%u])",
                         i, runningIndex, static_cast<unsigned>(nodeId));

            AbortSubtree(node.children[runningIndex], btComp, myTrans, asset, entity, dt);
            btComp.runningNodes[nodeId] = -1;
            runningIndex                = -1; // 下の再開を飛ばし、先頭から選び直させる
            break;
        }
    }

    // ---------------------------------------------------------
    // 2. 自己中断
    // ---------------------------------------------------------

    // 走っている枝に abortSelf が付いていて、その枝自身の先頭の条件が通らなくなったら、
    // 打ち切って先頭から選び直す。予備動作の最中に間合いを外れたら技を取りやめる、といった用途。

    // 割り込みと違って切り替え先を決めないので、0 番目の枝でも見る。
    // 割り込みで既に打ち切ったフレームは runningIndex が -1 なのでここは通らない。
    // 強制実行中は見ない（関門の素通し。理由は IgnoreGatesForDebug）。
    if (runningIndex >= 0 && runningIndex < (int)node.children.size() && !IgnoreGatesForDebug(btComp))
    {
        const BTNodeID runningChild = node.children[runningIndex];
        if (asset.nodes[runningChild].abortSelf &&
            !GuardsPass(runningChild, btComp, myTrans, asset, entity, dt) &&
            CanAbortSubtree(runningChild, btComp, myTrans, asset, entity, dt))
        {
            CCL_LOG_INFO(LogCategory::AI, "[BT] 自己中断: %d 番目の枝が条件を外れて降りた (nodes[%u])",
                         runningIndex, static_cast<unsigned>(nodeId));

            AbortSubtree(runningChild, btComp, myTrans, asset, entity, dt);
            btComp.runningNodes[nodeId] = -1;
            runningIndex                = -1;
        }
    }

    // ---------------------------------------------------------
    // 3. 再開
    // ---------------------------------------------------------
    if (runningIndex >= 0 && runningIndex < (int)node.children.size())
    {
        result = EvaluateTreeRecursive(node.children[runningIndex], btComp, myTrans, asset, entity, dt);
        if (result == BTNodeState::Running) return result;
        btComp.runningNodes[nodeId] = -1;
        if (result == BTNodeState::Success) return result;
    }

    // ---------------------------------------------------------
    // 4. 選び直し
    // ---------------------------------------------------------
    result = BTNodeState::Failure;
    for (size_t i = 0; i < node.children.size(); ++i)
    {
        result = EvaluateTreeRecursive(node.children[i], btComp, myTrans, asset, entity, dt);
        if (result == BTNodeState::Running)
        {
            btComp.runningNodes[nodeId] = (int)i;
            break;
        }
        if (result == BTNodeState::Success)
        {
            btComp.runningNodes[nodeId] = -1;
            break;
        }
    }
    return result;
}

BTNodeState BehaviorTreeSystem::EvaluateSequence(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    int startIndex   = 0;
    int runningIndex = btComp.runningNodes[nodeId];
    if (runningIndex >= 0 && runningIndex < (int)node.children.size())
    {
        // 直前に Running だった子があれば、そこから再開する
        startIndex = runningIndex;
    }

    // 子が１つもなければ即 Success。子があれば順に評価する。
    result = BTNodeState::Success;
    for (size_t i = startIndex; i < node.children.size(); ++i)
    {
        result = EvaluateTreeRecursive(node.children[i], btComp, myTrans, asset, entity, dt);
        if (result == BTNodeState::Running)
        {
            btComp.runningNodes[nodeId] = (int)i;
            break;
        }
        if (result == BTNodeState::Failure)
        {
            btComp.runningNodes[nodeId] = -1;
            ResetChildActionsRecursive(nodeId, btComp, asset);
            break;
        }
    }
    if (result == BTNodeState::Success)
    {
        // 成功したら再開位置を消し、配下の Action のタイマーも全部 -1 に戻す
        btComp.runningNodes[nodeId] = -1;
        ResetChildActionsRecursive(nodeId, btComp, asset);
    }
    return result;
}

BTNodeState BehaviorTreeSystem::EvaluateDecorator(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    // 生の int ではなく enum で分岐する（数値だとどれがどの修飾か読めない）。
    const BTDecoratorType decType = node.decoratorType;

    // クールダウン中は子を評価せず即 Failure。タイマーは Action の残り時間と
    // 同じ配列を共有するが、添字がノードIDなので衝突しない。
    // 強制実行中は止めない（関門の素通し。理由は IgnoreGatesForDebug）。
    if (decType == BTDecoratorType::Cooldown && btComp.nodeTimers[nodeId] > 0.0f &&
        !IgnoreGatesForDebug(btComp))
    {
        return BTNodeState::Failure;
    }

    if (node.children.empty())
    {
        return BTNodeState::Failure;
    }

    const BTNodeID childId = node.children[0];

    // Retry は子を複数回評価する。
    // 子が Success か Running ならループを抜ける。
    if (decType == BTDecoratorType::Retry)
    {
        // 同じフレームの中で繰り返し評価するので、
        // Condition で距離などを測っても結果は変わらない。
        // 使い道は、乱数や確率で失敗する行動を何度か試すくらい。

        // param は試行回数。0 だと一度も子を評価しないので最低1回は回す。
        const int retryCount = (std::max)(1, static_cast<int>(node.param));
        for (int r = 0; r < retryCount; ++r)
        {
            result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);
            if (result != BTNodeState::Failure) break;
        }
    }
    else
    {
        result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);
    }

    switch (decType)
    {
    case BTDecoratorType::Inverter:
        if (result == BTNodeState::Success)
            result = BTNodeState::Failure;
        else if (result == BTNodeState::Failure)
            result = BTNodeState::Success;
        break;

    case BTDecoratorType::Cooldown:
        // 子が最後まで終わったときだけクールダウンを始める。
        // Running で始めると技の途中からタイマーが進み、
        // Failure で始めると出していない技にクールダウンがかかる。
        if (result == BTNodeState::Success) btComp.nodeTimers[nodeId] = node.param;
        break;

    case BTDecoratorType::Retry:
        break; // 上のループで結果は確定済み
    }
    return result;
}

BTNodeState BehaviorTreeSystem::EvaluateParallel(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    // 主従型の並行ノード。0番目の子が「主」で、その結果をそのまま返す。
    // 1番目以降の「従」は主が走っている間だけ動かし、結果は捨てる。
    // 「技を出しながら、その間ドローンにも指示を出す」を書くためのノード。
    //
    // 主は毎フレーム評価され続けるので、毎フレームの後始末を持つ行動（currentActionId を書く技）をそのまま置ける。
    // 従には「毎フレーム評価されても安いもの」だけを置くこと。主が終わると従は評価されなくなるので、
    // 従に currentActionId を書く行動を置くと、要求が下りないまま残る（読み込み時に ValidateNodes が警告する）。
    //
    // 再開位置を持たないのが Selector / Sequence との違い。毎フレーム全部の子を
    // 評価し直すので、途中から再開する必要がない（各子のサブツリーは自分の再開位置で正しく再開する）。

    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    if (node.children.empty())
    {
        return BTNodeState::Failure;
    }

    // 主を評価する。結果はそのまま返す。
    result = EvaluateTreeRecursive(node.children[0], btComp, myTrans, asset, entity, dt);

    if (result == BTNodeState::Running)
    {
        // 従を評価する。戻り値は使わない。主が終わったフレームには評価しない
        // （終わった技の後に、従の指示だけが1回余計に出るのを避ける）。
        // 従は主が終わるまで評価され続けるので、最後まで行くと先頭から繰り返す。
        // 繰り返したくなければ、従の最後に長い待機を置く。
        for (size_t i = 1; i < node.children.size(); ++i)
        {
            EvaluateTreeRecursive(node.children[i], btComp, myTrans, asset, entity, dt);
        }
    }
    else
    {
        // 主が終わったので、この並行区間は終わり。従の実行状態を捨てる。
        //   捨てないと、従の中の行動の残り時間が 0.0 のまま残り、次に
        //   この枝へ来たとき「もう終わっている」と判定されて一度も実行されない
        //   （Action の後始末は、その行動が評価されたときにしか走らないため）。
        for (size_t i = 1; i < node.children.size(); ++i)
        {
            ResetSubtreeStateRecursive(node.children[i], btComp, asset);
        }
    }
    return result;
}

BTNodeState BehaviorTreeSystem::EvaluateWeightedSelector(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    // 子の weight に比例して1つ引き、失敗したらその枝を候補から外して引き直す。
    // 条件で弾かれる枝があっても「出せる技のどれか」が必ず出る。
    // 全部の重みが同じなら均等ランダムと同じ動作になる（既定の weight は 1.0）。

    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    // すでに Running だった子がいれば、まずその子を再評価する。
    // 引き直すと出している途中の技が変わってしまうため、抽選はしない。
    int runningIndex = btComp.runningNodes[nodeId];
    if (runningIndex >= 0 && runningIndex < (int)node.children.size())
    {
        result = EvaluateTreeRecursive(node.children[runningIndex], btComp, myTrans, asset, entity, dt);

        // 終わったら（Success でも Failure でも）再開位置を消す。
        // Failure でも、Selector のように他の子へは落ちない。今この場で引き直すと、
        // たった今失敗した枝をまた引く可能性がある。1フレーム譲って、次の
        // フレームに抽選からやり直す。
        if (result != BTNodeState::Running)
        {
            btComp.runningNodes[nodeId] = -1;
        }
        return result;
    }

    // 候補を作る。
    // どちらもスタック上の固定長配列で持つ。評価はワーカースレッドで走るので、
    // ここでヒープ確保をするとスレッド間でアロケータのロックを取り合う。
    BTNodeID cand[kMaxWeightedSelectorChildren];
    float    w[kMaxWeightedSelectorChildren];
    int      count = 0;
    float    total = 0.0f;

    // 上限を超える分を切り捨てる
    const int limit = (std::min)((int)node.children.size(), kMaxWeightedSelectorChildren);
    for (int i = 0; i < limit; ++i)
    {
        const float weight = asset.nodes[node.children[i]].weight;
        if (weight <= 0.0f) continue; // 重み0以下の枝は最初から入れない

        cand[count] = node.children[i];
        w[count]    = weight;
        total      += weight;
        ++count;
    }

    // 候補が尽きたら Failure（result の初期値のまま）
    while (count > 0)
    {
        // 重みに比例して1つ引く
        const int pick = PickWeighted(w, count, total, btComp.rngState);

        // 引いた枝を評価する
        const BTNodeID childId = cand[pick];
        result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);

        // 結果を確認する、失敗以外なら技を決定する
        if (result != BTNodeState::Failure)
        {
            // Running なら再開位置を残す。再開位置は node.children の添字で、候補配列は
            // 詰め替えているので元の並びから探し直す
            btComp.runningNodes[nodeId] = (result == BTNodeState::Running) ? IndexOfChild(node, childId) : -1;
            return result;
        }

        // 失敗した枝を候補から外す。末尾を詰めるので順番は崩れるが、
        // 抽選は重みだけで決まるので並びは関係ない。
        total     -= w[pick];
        cand[pick] = cand[count - 1];
        w[pick]    = w[count - 1];
        --count;
    }
    return result;
}


// 今出せる子だけに絞り、点数の高い上位3つから点数に比例して1つ引く。点数は ScoreUtilityChild が出す。
//
// WeightedSelector との違いは2つ。引く前に「今出せない子」（先頭の条件が通らない・クールダウン中）を
// 外すことと、引く範囲を上位3つに絞ること。出せない子が枠を占めないので、低い点の技が「数合わせ」で出ることがない。
// 割り込み・自己中断の印は Selector と同じように付けられる（割り込みは上下の代わりに点数で決める）。
BTNodeState BehaviorTreeSystem::EvaluateUtilitySelector(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    // 処理フロー:
    // 1. 割り込み：ほかの技の最中に、割り込みの印が付いた子の条件が通ったら、今の技を止める
    // 2. 再開：Running の子がいれば、その子だけを評価する（自己中断の印があり、条件が崩れたら降りて選び直す）
    // 3. ふるい：今出せる子だけを候補にする
    // 4. 点数の高い順に並べる
    // 5. 上位から抽選し、失敗したら外して詰める（1 で割り込んだ子は抽選せずに出す）
    //
    // 【備考】
    //   3 と 5 で、デバッグ表示用に点数・確率・選んだ子を btComp.debugScores に残す。
    //   選び直したときだけ書くので、技を出している間は「その技を選んだときの点数」が見え続ける。
    //   選べたら btComp.debugPickLog にも写す（すぐ次を選び直しても、前の選択が履歴に残る）。

    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    int runningIndex = btComp.runningNodes[nodeId];

    // ---------------------------------------------------------
    // 1. 割り込み
    // ---------------------------------------------------------

    // Selector の割り込みは「上の枝」が割り込むが、スコア選択の子には上下が無い。
    // 代わりに印の付いた子だけを見て、候補に入れる状態（条件・クールダウン・点数 > 0）になったら割り込む。
    // 割り込める子が複数あれば点数の高い方。割り込んだ子は 5 で抽選せずに出す。
    //
    // 割り込んで出た技は、別の割り込みでは止めない。止められると、印の付いた技同士が
    // 交互に止め合い、どちらも最後まで出ない。
    // 強制実行中は見ない（関門の素通し。理由は IgnoreGatesForDebug）。
    BTNodeID interruptChild = 0xFFFF; // 割り込んで出す子。0xFFFF は無し
    if (runningIndex >= 0 && runningIndex < (int)node.children.size() && !IgnoreGatesForDebug(btComp))
    {
        const BTNodeID runningChild = node.children[runningIndex];
        if (!asset.nodes[runningChild].interrupt)
        {
            const int limit     = (std::min)((int)node.children.size(), kMaxWeightedSelectorChildren);
            float     bestScore = 0.0f;
            for (int i = 0; i < limit; ++i)
            {
                const BTNodeID childId = node.children[i];
                if (i == runningIndex || !asset.nodes[childId].interrupt)
                {
                    continue;
                }
                if (!CandidatePasses(childId, btComp, myTrans, asset, entity, dt))
                {
                    continue;
                }

                // 点数の内訳はここでは要らない（デバッグの記録は 3 のふるいで書く）
                BTScoreDebug unused;
                const float  s = ScoreUtilityChild(childId, btComp, myTrans, asset, entity, dt, unused);
                if (s > bestScore)
                {
                    bestScore      = s;
                    interruptChild = childId;
                }
            }

            // 打ち切れない技が走っている間は見送る。次のフレームにまた見にくるので、
            // 空中の技なら着地した最初のフレームに入れ替わる
            if (interruptChild != 0xFFFF && CanAbortSubtree(runningChild, btComp, myTrans, asset, entity, dt))
            {
                CCL_LOG_INFO(LogCategory::AI, "[BT] 割り込み: スコア選択 nodes[%u] で nodes[%u] が nodes[%u] を打ち切った",
                             static_cast<unsigned>(nodeId), static_cast<unsigned>(interruptChild),
                             static_cast<unsigned>(runningChild));

                AbortSubtree(runningChild, btComp, myTrans, asset, entity, dt);
                btComp.runningNodes[nodeId] = -1;
                runningIndex                = -1; // 下の再開を飛ばし、このフレームのうちに選び直す
            }
            else
            {
                interruptChild = 0xFFFF;
            }
        }
    }

    // ---------------------------------------------------------
    // 2. 再開　前フレームで選んだ技を続けるため
    // ---------------------------------------------------------
    // Running の子がいれば、まずその子を再評価する。抽選はしない。

    // 自己中断：走っている子に abortSelf が付いていて、その子の先頭の条件が通らなくなったら降りる。
    // 遠距離の弾幕を出している間に近づかれたら、弾幕をやめて近距離の技を選び直す、といった用途。
    // Selector の自己中断と同じ判定（打ち切れない技が走っている間は見送る）。降りたら下の 3 へ進み、
    // このフレームのうちに選び直す。降りた子は条件が通らないので、ふるいで外れて選ばれ直さない。
    if (runningIndex >= 0 && runningIndex < (int)node.children.size() && !IgnoreGatesForDebug(btComp))
    {
        const BTNodeID runningChild = node.children[runningIndex];
        if (asset.nodes[runningChild].abortSelf &&
            !GuardsPass(runningChild, btComp, myTrans, asset, entity, dt) &&
            CanAbortSubtree(runningChild, btComp, myTrans, asset, entity, dt))
        {
            CCL_LOG_INFO(LogCategory::AI, "[BT] 自己中断: スコア選択 nodes[%u] の nodes[%u] が条件を外れて降りた",
                         static_cast<unsigned>(nodeId), static_cast<unsigned>(runningChild));

            AbortSubtree(runningChild, btComp, myTrans, asset, entity, dt);
            btComp.runningNodes[nodeId] = -1;
            runningIndex                = -1;
        }
    }

    if (runningIndex >= 0 && runningIndex < (int)node.children.size())
    {
        result = EvaluateTreeRecursive(node.children[runningIndex], btComp, myTrans, asset, entity, dt);

        // 終わったら再開位置を消す。Failure でも他の子へは落ちない。1フレーム譲って、次のフレームに選び直す
        if (result != BTNodeState::Running)
        {
            btComp.runningNodes[nodeId] = -1;
        }
        return result;
    }

    // ---------------------------------------------------------
    // 3. ふるい
    // ---------------------------------------------------------

    // 候補はスタック上の固定長配列で持つ ヒープ配列だと並列処理できないから
    BTNodeID cand[kMaxWeightedSelectorChildren];  // 候補の子のノードID
    float    score[kMaxWeightedSelectorChildren]; // 候補の子の点数
    int      count = 0;

    // 上限を超える分を切り捨てる
    const int limit = (std::min)((int)node.children.size(), kMaxWeightedSelectorChildren);

    // 点数が0以下の子は候補に入れない。条件で弾かれる子も候補に入れない。
    for (int i = 0; i < limit; ++i)
    {
        // 子供のi番目のノードID取得
        const BTNodeID childId = node.children[i];
        
        // 前回の記録を消す
        BTScoreDebug &dbg = btComp.debugScores[childId];
        dbg               = BTScoreDebug{};

        // その子供の点数を取得（優先度 × 距離 × 状況による倍率 × 繰り返しの減点。内訳は dbg に入る）
        const float    s       = ScoreUtilityChild(childId, btComp, myTrans, asset, entity, dt, dbg);
        dbg.total              = s;

        // 点数が０以下なら候補に入れない（理由はデバッグ表示用に残す。距離の倍率が 0 なら距離の外）
        if (s <= 0.0f)
        {
            dbg.state = (dbg.distance == 0.0f) ? BTCandidateState::OutOfRange : BTCandidateState::ZeroScore;
            continue;
        }

        // 条件で弾かれる子も候補に入れない
        if (!CandidatePasses(childId, btComp, myTrans, asset, entity, dt))
        {
            dbg.state = TopCooldownActive(childId, btComp, asset) ? BTCandidateState::Cooldown
                                                                  : BTCandidateState::Condition;
            continue;
        }

        // 候補に入れる
        dbg.state    = BTCandidateState::Candidate;
        cand[count]  = childId;
        score[count] = s;
        ++count;
    }

    // ---------------------------------------------------------
    // 4. 並べ替え
    // ---------------------------------------------------------

    // 多くても16個なので挿入ソートで足りる。同点は元の並び（node.children の順）を保つ
    for (int i = 1; i < count; ++i)
    {
        // 候補の子のノードIDと点数を一時退避
        const BTNodeID c = cand[i];
        const float    s = score[i];

        // ひとつ前の点数と比べ、点数が低い方を後ろへずらす。
        // 点数が同じなら元の並びを保つためにずらさない。
        int            j = i - 1;
        while (j >= 0 && score[j] < s) // 左の方が点数低い間なら
        {
            // 左の候補を１つ右へずらす
            cand[j + 1]  = cand[j];
            score[j + 1] = score[j];
            --j;
        }
        cand[j + 1]  = c;
        score[j + 1] = s;
    }

    // ---------------------------------------------------------
    // 5. 抽選
    // ---------------------------------------------------------

    // 失敗した子を外して詰めると次点が上位の枠へ繰り上がるので、上位を選び直す処理が要らない。
    // 候補が尽きたら Failure（result の初期値のまま）
    bool     firstDraw = true;
    BTNodeID firstTop[kUtilityTopCount]; // 最初の抽選の上位（デバッグの記録用。詰める前の並びを残す）
    int      firstTopCount = 0;
    while (count > 0)
    {
        // 上位3つまでの点数合計を計算する。上位3つの点数に比例して1つ引く
        const int top   = (std::min)(count, kUtilityTopCount); // 上位3つまで
        float     total = 0.0f;
        for (int i = 0; i < top; ++i)
        {
            total += score[i]; // 上位の点数合計を計算
        }

        // 確率は最初の抽選の分だけ残す。引き直しの確率で上書きすると、最初にどう見えていたかが分からなくなる
        if (firstDraw)
        {
            for (int i = 0; i < top; ++i)
            {
                btComp.debugScores[cand[i]].chance = score[i] / total;
                firstTop[i]                        = cand[i];
            }
            firstTopCount = top;
            firstDraw     = false;
        }

        // 1 で割り込んだ子は抽選せずに出す（上位3つの外でも出す）。失敗したら次からは普通に引く
        int pick = -1;
        if (interruptChild != 0xFFFF)
        {
            for (int i = 0; i < count; ++i)
            {
                if (cand[i] == interruptChild)
                {
                    pick = i;
                }
            }
            interruptChild = 0xFFFF;
        }

        const bool interrupted = (pick >= 0);
        if (!interrupted)
        {
            // 点数に比例して1つ引く
            pick = PickWeighted(score, top, total, btComp.rngState);
        }

        const BTNodeID childId = cand[pick];
        result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);

        if (result != BTNodeState::Failure)
        {
            // Running なら再開位置を残す。再開位置は node.children の添字で、候補配列は
            // 詰め替えているので元の並びから探し直す
            btComp.runningNodes[nodeId] = (result == BTNodeState::Running) ? IndexOfChild(node, childId) : -1;

            // 選んだ技はしばらく点数を下げる。同じ状況が続いても同じ技ばかり出さないため。
            // Failure の子は「出した」に数えない（出していない技まで出にくくなるため）
            btComp.repeatTimers[childId] = kRepeatPenaltySeconds;

            // どれを選んだかは画面の動きだけでは分かりにくいので、ログにも残す（Debug ビルドだけ出る）
            btComp.debugScores[childId].picked      = true;
            btComp.debugScores[childId].interrupted = interrupted;
            RecordUtilityPick(asset, nodeId, childId, firstTop, firstTopCount, btComp);
            CCL_LOG_INFO(LogCategory::AI, "[BT] スコア選択: nodes[%u] が nodes[%u] を選んだ（点数 %.1f%s）",
                         static_cast<unsigned>(nodeId), static_cast<unsigned>(childId), score[pick],
                         interrupted ? "・割り込み" : "");
            return result;
        }

        // 失敗した枝を外す。点数順を崩さないよう、後ろを1つずつ前へ詰める
        for (int i = pick; i + 1 < count; ++i)
        {
            cand[i]  = cand[i + 1];
            score[i] = score[i + 1];
        }
        --count;
    }
    return result;
}

float BehaviorTreeSystem::ScoreUtilityChild(BTNodeID                  childId,
                                            BehaviorTreeComponent    &btComp,
                                            const TransformComponent &myTrans,
                                            const BTAsset            &asset,
                                            EntityID                  entity,
                                            float                     dt,
                                            BTScoreDebug             &outDebug)
{
    // 処理フロー:
    // 1. 優先度 × 距離の倍率（ルールが無ければ weight）
    // 2. 状況による倍率：当てはまる条件の倍率を全部掛ける
    // 3. 繰り返しの減点を掛ける

    // ---------------------------------------------------------
    // 1. 優先度 × 距離
    // ---------------------------------------------------------
    const float baseScore = ScoreChild(asset, childId, btComp.blackboard.distanceToTarget, &outDebug.distance);

    // ---------------------------------------------------------
    // 2. 状況による倍率
    // ---------------------------------------------------------

    // 判定には BT の条件ノードと同じ条件を使う。判断材料を足すときは条件を1つ足せばよく、
    // 採点ルールの形も読み込みもエディタも直さずに済む。
    // ctx の nodeId は子のものを渡す（条件は nodeTimers を読まないので、どのノードの ID でも結果は同じ）
    outDebug.bonus      = 1.0f;
    const BTNode &child = asset.nodes[childId];
    if (child.scoreRuleIndex >= 0 && child.scoreRuleIndex < static_cast<int>(asset.scoreRules.size()))
    {
        const BTScoreRule &rule = asset.scoreRules[child.scoreRuleIndex];
        for (int i = 0; i < rule.bonusCount; ++i)
        {
            const BTScoreBonus &bonus = rule.bonuses[i];

            IBTCondition *cond = BTActionRegistry::Instance().GetCondition(bonus.conditionId);
            if (!cond)
            {
                continue; // 未登録の条件は「当てはまらない」扱い（起動時の VerifyRegistrations が知らせる）
            }

            BTContext ctx = MakeContext(childId, btComp, myTrans, entity, dt, bonus.param);
            if (cond->Evaluate(ctx))
            {
                outDebug.bonus *= bonus.mult;
            }
        }
    }

    // ---------------------------------------------------------
    // 3. 繰り返しの減点
    // ---------------------------------------------------------

    // ルールの無い子（weight だけの子）にも掛ける。同じ技ばかり出さないのはスコア選択そのものの性質なので
    outDebug.repeat = RepeatFactor(btComp.repeatTimers[childId]);

    return baseScore * outDebug.bonus * outDebug.repeat;
}
