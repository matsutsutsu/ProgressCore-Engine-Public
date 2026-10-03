#include "BehaviorTreeSystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/BTAssetCache.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTConditions.h"     // 汎用条件
#include "Game/Logics/Character/Boss/Actions/BTConditions_Boss.h" // ボス固有条件
#include "Game/Logics/Character/Boss/Actions/BTActions_Boss.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/App/GameFlowState.h" // カットシーン中はAIを止める

#include <algorithm>
#include <cstdio>

#include <imgui.h>
// デバッグ UI で技を止めるときにだけ使う
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

using namespace CCL::ECS;
using namespace CCL::AI;
using namespace CCL::AI::BT;

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

    // 0 は「未初期化」の印なので必ず非ゼロにする。
    void SeedRandomIfNeeded(BehaviorTreeComponent &btComp, CCL::ECS::EntityID entity)
    {
        if (btComp.rngState != 0) return;
        btComp.rngState = (static_cast<uint32_t>(entity) * 2654435761u) ^ 0x9E3779B9u;
        if (btComp.rngState == 0) btComp.rngState = 0x6D2B79F5u;
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

    // 指定したサブツリーの下にある Action ノードのタイマーだけを -1 に戻す。
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

BehaviorTreeSystem::BehaviorTreeSystem() : IfSystem("BehaviorTreeSystem")
{
}

void BehaviorTreeSystem::Initialize()
{
    // 敵の種類を足したら、その敵の登録関数をここに1行足す。自己登録マクロにも
    // できるが、どの敵の行動が登録されているかを1画面で見られる方を選んでいる。
    BT::RegisterCommonConditions();
    BT::RegisterBossConditions();
    BT::RegisterBossActions();

    VerifyRegistrations();

    CCL_LOG_INFO(LogCategory::AI, "[BehaviorTreeSystem] BTノード登録完了");
}

void BehaviorTreeSystem::VerifyRegistrations()
{
    // 辞書（ActionRegistry.h）と実装のレジストリを突き合わせる。辞書に載っていれば
    // エディタの候補には出るが、登録関数に書き忘れると評価側が何も言わずに Failure を
    // 返す。「選べるのに置いても何も起きない技」になり、原因を見つけにくい。
    const auto &reg = BTActionRegistry::Instance();

    int missing = 0;
    for (int i = 0; i < g_ActionRegistryCount; ++i)
    {
        const ActionEntry &e = g_ActionRegistry[i];
        if (e.id == 0) continue; // None は実装を持たない
        if (reg.HasAction(e.id)) continue;

        CCL_LOG_ERROR(LogCategory::AI,
                      "[BT] 行動 \"%s\"(ID %u) は辞書にあるが実装が登録されていない。"
                      "木に置いても何も起きず Failure になる（登録関数への追加漏れ）",
                      e.key, static_cast<unsigned>(e.id));
        ++missing;
    }
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
    {
        const ActionEntry &e = g_ConditionRegistry[i];
        if (e.id == 0) continue;
        if (reg.HasCondition(e.id)) continue;

        CCL_LOG_ERROR(LogCategory::AI,
                      "[BT] 条件 \"%s\"(ID %u) は辞書にあるが実装が登録されていない。"
                      "常に false になる（登録関数への追加漏れ）",
                      e.key, static_cast<unsigned>(e.id));
        ++missing;
    }

    if (missing == 0)
    {
        CCL_LOG_INFO(LogCategory::AI,
                     "[BT] 辞書と実装の突き合わせOK（行動 %d / 条件 %d）。"
                     "スケジューラへの追加申告: 書き込み %zu 型 / 読み取り %zu 型",
                     g_ActionRegistryCount - 1, g_ConditionRegistryCount - 1,
                     reg.ExtraWriteTypes().size(), reg.ExtraReadTypes().size());
    }
}

std::vector<CCL::ECS::TypeID> BehaviorTreeSystem::GetReadTypes() const
{
    auto        types = Base::GetReadTypes();
    const auto &extra = BTActionRegistry::Instance().ExtraReadTypes();
    types.insert(types.end(), extra.begin(), extra.end());
    return types;
}

std::vector<CCL::ECS::TypeID> BehaviorTreeSystem::GetWriteTypes() const
{
    auto        types = Base::GetWriteTypes();
    const auto &extra = BTActionRegistry::Instance().ExtraWriteTypes();
    types.insert(types.end(), extra.begin(), extra.end());
    return types;
}

void BehaviorTreeSystem::SyncAssets()
{
    auto &cache = BTAssetCache::Instance();

    // 直列で回す。やるのはパスに対応する木を受け取り直すことだけで、変化が無ければ
    // ポインタの比較1回で終わる。ここを並列にしてはいけない（キャッシュに無いとき、
    // GetOrLoad のファイル I/O がワーカースレッドから走る）。
    ForEach([&](BehaviorTreeComponent &btComp,
                const TransformComponent & /*trans*/,
                const TimeStateComponent & /*time*/) {
        // パスが空なら nullptr。パスがあればキャッシュから取る（無ければ読み込む）
        std::shared_ptr<const BTAsset> desired =
            btComp.assetPath.empty() ? nullptr : cache.GetOrLoad(btComp.assetPath);

        // アセットが変更されていなければ何もしない
        if (desired == btComp.sharedAsset) return; // 大半のフレームはここで終わる

        btComp.sharedAsset     = desired;
        btComp.loadedAssetPath = desired ? btComp.assetPath : std::string();

        // 木が入れ替わったら実行状態は捨てる。前の木の途中経過を持ち越すと、差し替え直後の
        // 1回だけ挙動がおかしくなる。バッファの確保もここだけで行い、評価側（並列）では確保しない。
        const size_t nodeCount = desired ? desired->nodes.size() : 0;
        btComp.debugNodeStates.assign(nodeCount, BTDebugState::None);
        btComp.nodeTimers.assign(nodeCount, -1.0f);
        btComp.runningNodes.assign(nodeCount, -1);
        btComp.runningNodeId = 0xFFFF;
    });
}

void BehaviorTreeSystem::Update(float rawDt)
{
    if (!m_world->HasResource<GameFlowState>()) return;

    // カットシーン中はAIの思考を止める
    const bool cinematic = m_world->GetResource<GameFlowState>().cinematic;

    // 1. 各個体へ木を配る（直列）
    SyncAssets();

    // 2. 評価（並列）
    ForEachWithIDParallel([this, cinematic](EntityID                  entity,
                                            BehaviorTreeComponent    &btComp,
                                            const TransformComponent &trans,
                                            const TimeStateComponent &time) {
        float dt = time.localDt;

        if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty()) return;

        const size_t nodeCount = btComp.sharedAsset->nodes.size();

        // バッファは SyncAssets がそろえてある。並列ループ内で伸ばさず、ずれていたら見送る。
        if (btComp.nodeTimers.size() != nodeCount) return;

        // 一時停止中（強制実行を除く）とカットシーン中は評価しない
        const bool forcing = (btComp.debugForcedNodeId >= 0);
        if ((btComp.debugPaused && !forcing) || cinematic)
        {
            std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);
            btComp.runningNodeId = 0xFFFF;
            return;
        }

        // タイマーを減らす
        for (size_t i = 0; i < btComp.nodeTimers.size(); ++i)
        {
            if (btComp.nodeTimers[i] > 0.0f)
            {
                btComp.nodeTimers[i] -= dt;
                if (btComp.nodeTimers[i] <= 0.0f) btComp.nodeTimers[i] = 0.0f;
            }
        }
        std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);

        btComp.runningNodeId = 0xFFFF;

        // 抽選の乱数の種は個体ごとに1回だけ決める
        SeedRandomIfNeeded(btComp, entity);

        // 強制ノードが切り替わったら実行状態を捨てる（前の技の途中経過が新しい枝へ
        // 持ち越されると、1回目だけ挙動がおかしくなる）。
        if (btComp.debugForcedNodeId != btComp.debugAppliedForcedNodeId)
        {
            btComp.debugAppliedForcedNodeId = btComp.debugForcedNodeId;
            std::fill(btComp.runningNodes.begin(), btComp.runningNodes.end(), -1);
            std::fill(btComp.nodeTimers.begin(), btComp.nodeTimers.end(), -1.0f);
        }

        // 強制指定があればその枝から、無ければルート(0)から評価する。木の構造は変えない。
        BTNodeID startNode = 0;
        if (btComp.debugForcedNodeId >= 0 && btComp.debugForcedNodeId < static_cast<int>(nodeCount))
        {
            startNode = static_cast<BTNodeID>(btComp.debugForcedNodeId);
        }
        EvaluateTreeRecursive(startNode, btComp, trans, *btComp.sharedAsset, entity, dt);
    });
}

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

bool BehaviorTreeSystem::GuardsPass(BTNodeID                  nodeId,
                                    BehaviorTreeComponent    &btComp,
                                    const TransformComponent &myTrans,
                                    const BTAsset            &asset,
                                    EntityID                  entity,
                                    float                     dt)
{
    // どんな形なら条件を先に調べられるかは HasReadableGuard が決める。ここは同じ形をたどって条件を評価する。
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
            if (!HasReadableGuard(asset, childId)) break;
            if (!GuardsPass(childId, btComp, myTrans, asset, entity, dt)) return false;
        }
        return true;

    default: return false;
    }
}

bool BehaviorTreeSystem::CanAbortSubtree(BTNodeID                  nodeId,
                                         BehaviorTreeComponent    &btComp,
                                         const TransformComponent &myTrans,
                                         const BTAsset            &asset,
                                         EntityID                  entity,
                                         float                     dt)
{
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

BTNodeState BehaviorTreeSystem::EvaluateTreeRecursive(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    switch (node.type)
    {
    case BTNodeType::Selector: {
        int runningIndex = btComp.runningNodes[nodeId];

        // 割り込み。走っている枝より上に interrupt の付いた枝があり、その先頭の条件が通るなら、
        // 走っている枝を打ち切ってそちらへ切り替える。
        //
        //   強制実行中は見ない。「この枝だけを実行」の最中に別の枝へ移ると、押した本人が
        //   見たいものを見られない。
        //
        //   注意：割り込んだ枝がすぐ Failure になると、下の枝を毎フレーム打ち切っては
        //   やり直すことになり、技が一度も最後まで出ない。先頭の条件は「通ったら本当にその枝が
        //   動く」ものにすること（被弾の枝が蓄積をリセットして自分で抜けるのが良い例）。
        if (runningIndex > 0 && runningIndex < (int)node.children.size() && btComp.debugForcedNodeId < 0)
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

        // 自己中断。走っている枝に abortSelf が付いていて、その枝自身の先頭の条件が通らなくなったら、
        // 打ち切って先頭から選び直す。割り込みと違って切り替え先を決めないので、0 番目の枝でも見る。
        //
        //   割り込みで既に打ち切ったフレームは runningIndex が -1 なのでここは通らない。
        if (runningIndex >= 0 && runningIndex < (int)node.children.size() && btComp.debugForcedNodeId < 0)
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

        // 直前に Running だった子がいれば、まずその子を再評価する。
        if (runningIndex >= 0 && runningIndex < (int)node.children.size())
        {
            result = EvaluateTreeRecursive(node.children[runningIndex], btComp, myTrans, asset, entity, dt);
            if (result == BTNodeState::Running) break;
            btComp.runningNodes[nodeId] = -1;
            if (result == BTNodeState::Success) break;
        }

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
        break;
    }

    case BTNodeType::Sequence: {
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
        break;
    }

    case BTNodeType::Condition: {
        // 強制実行中は無条件に通す。間合いの条件が付いた枝はその場の距離次第で失敗し、
        // 「実行」を押しても反応しないように見えてしまう。
        if (btComp.debugForcedNodeId >= 0)
        {
            result = BTNodeState::Success;
            break;
        }

        result = EvaluateCondition(nodeId, btComp, myTrans, asset, entity, dt) ? BTNodeState::Success
                                                                              : BTNodeState::Failure;
        break;
    }

    case BTNodeType::Action: {
        IBTAction *action = BTActionRegistry::Instance().GetAction(node.actionOrConditionId);
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

    case BTNodeType::Decorator: {
        // 生の int ではなく enum で分岐する（数値だとどれがどの修飾か読めない）。
        const BTDecoratorType decType = node.decoratorType;

        // クールダウン中は子を評価せず即 Failure。タイマーは Action の残り時間と
        // 同じ配列を共有するが、添字がノードIDなので衝突しない。
        // 強制実行中は止めない（押したのに何も起きないと、見たいものが見られない）。
        if (decType == BTDecoratorType::Cooldown && btComp.nodeTimers[nodeId] > 0.0f &&
            btComp.debugForcedNodeId < 0)
        {
            result = BTNodeState::Failure;
            break;
        }

        if (node.children.empty()) break;

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
        break;
    }

    // 主従型の並行ノード。0番目の子が「主」で、その結果をそのまま返す。
    //   1番目以降の「従」は主が走っている間だけ動かし、結果は捨てる。
    //   再開位置を持たないのが Selector / Sequence との違い。毎フレーム全部の子を
    //   評価し直すので、途中から再開する必要がない
    //   （各子のサブツリーは自分の再開位置で正しく再開する）。
    case BTNodeType::Parallel: {
        if (node.children.empty()) break;   // result は Failure のまま

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
        break;
    }

    // 子の weight に比例して1つ引き、失敗したらその枝を候補から外して引き直す。
    //   全部の重みが同じなら均等ランダムと同じ動作になる。
    case BTNodeType::WeightedSelector: {
        // すでに Running だった子がいれば、まずその子を再評価する。
        // 引き直すと出している途中の技が変わってしまうため、抽選はしない。
        int runningIndex = btComp.runningNodes[nodeId];
        if (runningIndex >= 0 && runningIndex < (int)node.children.size())
        {
            result = EvaluateTreeRecursive(node.children[runningIndex], btComp, myTrans, asset, entity, dt);
            if (result == BTNodeState::Running) break;
            btComp.runningNodes[nodeId] = -1;

            // Failure でも、Selector のように他の子へは落ちない。今この場で引き直すと、
            // たった今失敗した枝をまた引く可能性がある。1フレーム譲って、次の
            // フレームに抽選からやり直す。
            break;
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

        result = BTNodeState::Failure;
        while (count > 0)
        {
            // 重みに比例して1つ引く。累積和を乱数が超えた所が当選。
            float r    = NextRandom01(btComp.rngState) * total;
            int   pick = count - 1; // 丸め誤差で最後まで超えなかったときの保険
            for (int i = 0; i < count; ++i)
            {
                r -= w[i];
                if (r <= 0.0f) { pick = i; break; }
            }

            const BTNodeID childId = cand[pick];
            result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);

            if (result != BTNodeState::Failure)
            {
                btComp.runningNodes[nodeId] = -1;
                if (result == BTNodeState::Running)
                {
                    // 再開位置は node.children の添字。候補配列は詰め替えているので、
                    // 元の並びから探し直す。
                    for (size_t origIdx = 0; origIdx < node.children.size(); ++origIdx)
                    {
                        if (node.children[origIdx] == childId)
                        {
                            btComp.runningNodes[nodeId] = (int)origIdx;
                            break;
                        }
                    }
                }
                break;
            }

            // 失敗した枝を候補から外す。末尾を詰めるので順番は崩れるが、
            // 抽選は重みだけで決まるので並びは関係ない。
            total     -= w[pick];
            cand[pick] = cand[count - 1];
            w[pick]    = w[count - 1];
            --count;
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

namespace
{
    // ActionRegistry.h の辞書から表示名を逆引きする。見つからなければ nullptr。
    const char *LookupEntryName(const ActionEntry *table, int count, ActionID id)
    {
        for (int i = 0; i < count; ++i)
        {
            if (table[i].id == id) return table[i].name;
        }
        return nullptr;
    }

    /**
     * @brief この枝で最初に見つかる Action の表示名を返す。
     *
     * Sequence や Selector は名前を持たないので、その枝が何をするかを中の先頭のアクションで示す。
     *
     * @param asset  木の構造。
     * @param nodeId 調べる枝の根のノード。
     * @return 行動の表示名（ActionRegistry の文字列）。枝に Action が無ければ nullptr。
     */
    const char *FirstActionNameInSubtree(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action)
        {
            return LookupEntryName(g_ActionRegistry, g_ActionRegistryCount, node.actionOrConditionId);
        }
        for (BTNodeID child : node.children)
        {
            if (const char *name = FirstActionNameInSubtree(asset, child)) return name;
        }
        return nullptr;
    }

    // param を小数第1位までの文字列にする（10.0 → "10.0"）。
    std::string FormatParam(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        return buf;
    }

    const char *NodeTypeName(BTNodeType type)
    {
        switch (type)
        {
        case BTNodeType::Selector:
            return "Selector";
        case BTNodeType::Sequence:
            return "Sequence";
        case BTNodeType::Condition:
            return "Condition";
        case BTNodeType::Action:
            return "Action";
        case BTNodeType::Decorator:
            return "Decorator";
        case BTNodeType::WeightedSelector:
            return "WeightedSelector";
        case BTNodeType::Parallel:
            return "Parallel";
        default:
            return "Unknown";
        }
    }
} // namespace

void BehaviorTreeSystem::DrawNodeRowRecursive(const BTAsset         &asset,
                                              BTNodeID               nodeId,
                                              BehaviorTreeComponent &btComp,
                                              EntityID               entity)
{
    const BTNode &node = asset.nodes[nodeId];

    // 行のラベル（種類＋中身が分かる名前）。
    std::string label = NodeTypeName(node.type);
    if (node.type == BTNodeType::Action)
    {
        const char *name = LookupEntryName(g_ActionRegistry, g_ActionRegistryCount, node.actionOrConditionId);
        label += name ? std::string(" : ") + name
                      : std::string(" : id=") + std::to_string(node.actionOrConditionId);
        if (GetActionParamLabel(node.actionOrConditionId))
            label += " (" + FormatParam(node.param) + ")";
    }
    else if (node.type == BTNodeType::Condition)
    {
        const char *name = LookupEntryName(g_ConditionRegistry, g_ConditionRegistryCount, node.actionOrConditionId);
        label += name ? std::string(" : ") + name
                      : std::string(" : id=") + std::to_string(node.actionOrConditionId);
        // param を出さないと「距離が N m 未満」が並ぶだけで何mの判定か分からない。
        if (GetConditionParamLabel(node.actionOrConditionId))
            label += " (" + FormatParam(node.param) + ")";
    }
    else if (const char *hint = FirstActionNameInSubtree(asset, nodeId))
    {
        label += std::string(" [") + hint + "]";
    }

    // 実行状態を数字で出す。色だけでは「なぜこのノードから進まないのか」が分からない。
    //   行動 … 残り時間。0.00 なら待ち終わっている、-1 は未実行
    //   複合 … 今どの子で止まっているか（再開位置）。変わらなければその子が Running のまま
    //   Cooldown … 残りクールダウン。>0 の間はこの枝ごと Failure
    char buf[64];
    if (nodeId < btComp.nodeTimers.size())
    {
        const float t = btComp.nodeTimers[nodeId];
        if (node.type == BTNodeType::Action && t >= 0.0f)
        {
            std::snprintf(buf, sizeof(buf), "  残り %.2fs", t);
            label += buf;
        }
        else if (node.type == BTNodeType::Decorator &&
                 node.decoratorType == BTDecoratorType::Cooldown)
        {
            std::snprintf(buf, sizeof(buf), "  CD残り %.2fs", (t > 0.0f) ? t : 0.0f);
            label += buf;
        }
    }
    if (nodeId < btComp.runningNodes.size() && !node.children.empty())
    {
        const int bookmark = btComp.runningNodes[nodeId];
        if (bookmark >= 0)
        {
            std::snprintf(buf, sizeof(buf), "  再開位置 %d/%d",
                          bookmark + 1, static_cast<int>(node.children.size()));
            label += buf;
        }
    }

    ImGui::PushID(static_cast<int>(nodeId));

    // 実行ボタン（もう一度押すと解除）。
    const bool isForced = (btComp.debugForcedNodeId == static_cast<int>(nodeId));
    if (ImGui::SmallButton(isForced ? "解除" : "実行"))
    {
        btComp.debugForcedNodeId = isForced ? -1 : static_cast<int>(nodeId);

        // 前の技の「今撃て」信号が残ると、止めたはずのドローンが飛び続ける。
        // 評価の処理にはボス固有の知識を持たせないので、この後始末はデバッグ UI だけで行う。
        if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity))
        {
            cmd->droneAttackActive = false;
        }
    }
    ImGui::SameLine();

    // 今フレームの評価結果で色を変える（走っている枝が一目で分かる）。
    ImVec4 color(0.75f, 0.75f, 0.75f, 1.0f);
    if (nodeId < btComp.debugNodeStates.size())
    {
        switch (btComp.debugNodeStates[nodeId])
        {
        case BTDebugState::Running:
            color = ImVec4(1.00f, 0.85f, 0.20f, 1.0f);
            break;
        case BTDebugState::Success:
            color = ImVec4(0.40f, 1.00f, 0.50f, 1.0f);
            break;
        case BTDebugState::Failure:
            color = ImVec4(0.90f, 0.45f, 0.40f, 1.0f);
            break;
        default:
            break;
        }
    }

    if (node.children.empty())
    {
        ImGui::TextColored(color, "#%d %s", static_cast<int>(nodeId), label.c_str());
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_DefaultOpen,
                                            "#%d %s", static_cast<int>(nodeId), label.c_str());
        ImGui::PopStyleColor();
        if (open)
        {
            for (BTNodeID child : node.children)
            {
                DrawNodeRowRecursive(asset, child, btComp, entity);
            }
            ImGui::TreePop();
        }
    }

    ImGui::PopID();
}

void BehaviorTreeSystem::DrawForcedNodeGui(BehaviorTreeComponent &btComp, EntityID entity)
{
    if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty())
    {
        ImGui::TextDisabled("BT が読み込まれていません");
        return;
    }

    // 木の評価を止める。移動中に止めるとボスが歩き続けるので、行動の要求も合わせて消す。
    bool paused = btComp.debugPaused;
    if (ImGui::Checkbox("AIを止める（調整に集中）", &paused))
    {
        btComp.debugPaused = paused;
        if (paused)
        {
            if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity))
            {
                cmd->currentActionId = 0; // None → BossActionSystem は Idle
            }
        }
    }
    if (btComp.debugPaused)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           (btComp.debugForcedNodeId >= 0)
                               ? "強制実行中はこのチェックは効かない"
                               : "停止中（陣形指示は保持される）");
    }

    if (btComp.debugForcedNodeId >= 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "強制実行中: #%d", btComp.debugForcedNodeId);
        ImGui::SameLine();
        if (ImGui::SmallButton("通常のBTへ戻す"))
        {
            btComp.debugForcedNodeId = -1;
            if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity))
            {
                cmd->droneAttackActive = false;
            }
        }
    }
    else
    {
        ImGui::TextDisabled("通常評価（ルートから）");
    }

    ImGui::TextDisabled("「実行」を押した枝だけを繰り返し評価します。");
    ImGui::TextDisabled("強制中は間合いなどの条件をすべて通し、クールダウンも無視します。");
    ImGui::TextDisabled("技を丸ごと見るなら Sequence の行を、単発で見るなら Action の行を押す。");
    ImGui::Separator();

    DrawNodeRowRecursive(*btComp.sharedAsset, 0, btComp, entity);
}

void BehaviorTreeSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Behavior Tree (Brain Debugger)")) return;

    auto view = m_world->View<BehaviorTreeComponent>();
    for (auto e : view)
    {
        auto *btComp = m_world->GetComponent<BehaviorTreeComponent>(e);
        if (!btComp) continue;

        ImGui::PushID(static_cast<int>(e));
        ImGui::Text("Entity ID: %llu", (unsigned long long)e);
        ImGui::Text("Asset Path: %s", btComp->assetPath.c_str());

        // lastLogActionId から行動名を逆引きする。行動が自分で書く値なので、A_Wait のように
        // 書かない行動の間は前の行動が残る（見出しを Last にしているのはそのため）。
        const ActionEntry *lastAction = FindActionEntry(btComp->lastLogActionId);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Last Action: %s", lastAction ? lastAction->name : "Idle");

        ImGui::Text("Distance To Target: %.2fm", btComp->blackboard.distanceToTarget);

        // 角度と接近速度は符号に意味があるので、読み方を横に出す（右か左か、詰めか引きか）。
        ImGui::Text("Angle To Target  : %+.1f deg  [%s]", btComp->blackboard.angleToTarget,
                    (btComp->blackboard.angleToTarget >= 0.0f) ? "右" : "左");
        ImGui::Text("Closing Speed    : %+.2f m/s  [%s]", btComp->blackboard.targetClosingSpeed,
                    (btComp->blackboard.targetClosingSpeed >= 0.0f) ? "接近" : "後退");
        ImGui::Text("To Arena Edge    : %+.1f m", btComp->blackboard.distanceToArenaEdge);
        ImGui::Text("Target vs Center : %.1f deg  [0=中央側を塞がれている]", btComp->blackboard.targetAngleFromCenter);

        ImGui::Text("HP: %.1f%%", btComp->blackboard.healthPercentage);

        // 木を並べて「この技を今すぐ出す」を選べるようにする。
        if (ImGui::TreeNodeEx("強制実行（デバッグ）", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawForcedNodeGui(*btComp, e);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

REGISTER_LOGIC_SYSTEM(BehaviorTreeSystem, Priority::LogicStage::L02_Update);
