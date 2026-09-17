// 汎用 BehaviorTree 評価エンジンの実装。行動・条件は registry から引く（switch は使わない）。
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
// デバッグ用に使うのでここに置く
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

using namespace CCL::ECS;
using namespace CCL::AI;
using namespace CCL::AI::BT;

namespace
{
    // xorshift32。質は要らないが再現性は要る。状態を BehaviorTreeComponent が
    // 持つので個体ごとに独立し、ワーカースレッドをまたいでも列が混ざらない。
    uint32_t NextRandom(uint32_t &state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /// [0,1) の実数を1つ引く。上位24bitだけ使う（xorshift は下位bitの質が落ちる）。
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

    /// サブツリーの実行状態（しおりと行動の残り時間）をまとめて捨てる。
    ///
    /// ResetChildActionsRecursive との違いは、しおり(runningNodes)も消すこと。
    /// Parallel が「従」を切り離すときに使う。従は次のフレームから評価されなく
    /// なるので、途中のしおりと残り時間を残すと、次にこの枝へ来たときに
    /// 「前回の続き」から再開してしまう。
    ///
    /// Cooldown のタイマーは消さない（Action だけ -1 に戻す）。クールダウンは
    /// 枝をまたいで持ち越すべき情報で、ここで消すと封印が解けてしまう。
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

    /// 特定のサブツリー配下の Action ノードのタイマーだけをリセットする。
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
    // 敵の種類を足したら、その敵の RegisterAll をここに1行足す。自己登録マクロにも
    // できるが、「どの敵の行動が生きているか」が1画面で分かる利点のほうが大きい。
    BT::CommonConditions::RegisterAll();
    BT::BossConditions::RegisterAll();
    BT::BossActions::RegisterAll();

    VerifyRegistrations();

    CCL_LOG_INFO(LogCategory::AI, "[BehaviorTreeSystem] BTノード登録完了");
}

void BehaviorTreeSystem::VerifyRegistrations()
{
    // 辞書（ActionRegistry.h）と実装レジストリの突き合わせ。辞書に載っていれば
    // エディタの候補には出るが、RegisterAll に書き忘れると評価側が黙って Failure を
    // 返す＝「選べるのに置いても何も起きない技」という一番たちの悪い形になる。
    const auto &reg = BTActionRegistry::Instance();

    int missing = 0;
    for (int i = 0; i < g_ActionRegistryCount; ++i)
    {
        const ActionEntry &e = g_ActionRegistry[i];
        if (e.id == 0) continue; // None は実装を持たない
        if (reg.HasAction(e.id)) continue;

        CCL_LOG_ERROR(LogCategory::AI,
                      "[BT] 行動 \"%s\"(ID %u) は辞書にあるが実装が登録されていない。"
                      "木に置いても何も起きず Failure になる（RegisterAll への追加漏れ）",
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
                      "常に false になる（RegisterAll への追加漏れ）",
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

    // 直列で回す。やるのは「パスに対応する木を受け取り直す」だけで、変化が無ければ
    // ポインタ比較1回で終わる。ここを並列にしてはいけない（キャッシュが外れた瞬間、
    // GetOrLoad のファイルI/Oがワーカースレッドから走る）。
    ForEach([&](BehaviorTreeComponent &btComp,
                const TransformComponent & /*trans*/,
                const TimeStateComponent & /*time*/) {
        // パスが空の場合はnullptrを返す
        // パスがあれば実際にファイルを読み込んでアセットを取得する
        std::shared_ptr<const BTAsset> desired =
            btComp.assetPath.empty() ? nullptr : cache.GetOrLoad(btComp.assetPath);

        // アセットが変更されていなければ何もしない
        if (desired == btComp.sharedAsset) return; // 大半のフレームはここで終わる

        btComp.sharedAsset     = desired;
        btComp.loadedAssetPath = desired ? btComp.assetPath : std::string();

        // 木が入れ替わったら実行状態は捨てる。前の木の途中経過を持ち越すと差し替え直後の
        // 1回だけ挙動がおかしくなる。バッファ確保もここに集約し、評価側（並列）は確保しない。
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

    // ① BTアセットのセットアップ
    SyncAssets();

    // ② 評価（並列）。
    ForEachWithIDParallel([this, cinematic](EntityID                  entity,
                                            BehaviorTreeComponent    &btComp,
                                            const TransformComponent &trans,
                                            const TimeStateComponent &time) {
        float dt = time.localDt;

        // アセットが無いならreturn
        if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty()) return;

        const size_t nodeCount = btComp.sharedAsset->nodes.size();

        // バッファは SyncAssets が揃えてある。並列ループ内で伸ばさず、ずれていたら見送る。
        if (btComp.nodeTimers.size() != nodeCount) return;

        // 一時停止やカットシーン中はreturnで止める
        const bool forcing = (btComp.debugForcedNodeId >= 0);
        if ((btComp.debugPaused && !forcing) || cinematic)
        {
            std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);
            btComp.runningNodeId = 0xFFFF;
            return;
        }

        // タイマー減算。
        for (size_t i = 0; i < btComp.nodeTimers.size(); ++i)
        {
            if (btComp.nodeTimers[i] > 0.0f)
            {
                btComp.nodeTimers[i] -= dt;
                if (btComp.nodeTimers[i] <= 0.0f) btComp.nodeTimers[i] = 0.0f;
            }
        }
        std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);

        // Running しおりのリセット。
        btComp.runningNodeId = 0xFFFF;

        // 抽選の種は個体ごとに1回だけ蒔く
        SeedRandomIfNeeded(btComp, entity);

        // 強制ノードが切り替わったら実行状態を捨てる（前の技の途中経過が新しい枝へ
        // 持ち越されると、1回目だけ挙動がおかしくなる）。
        if (btComp.debugForcedNodeId != btComp.debugAppliedForcedNodeId)
        {
            btComp.debugAppliedForcedNodeId = btComp.debugForcedNodeId;
            std::fill(btComp.runningNodes.begin(), btComp.runningNodes.end(), -1);
            std::fill(btComp.nodeTimers.begin(), btComp.nodeTimers.end(), -1.0f);
        }

        // 強制指定があればその枝から、無ければルート(0)から。木の構造には触れない。
        BTNodeID startNode = 0;
        if (btComp.debugForcedNodeId >= 0 && btComp.debugForcedNodeId < static_cast<int>(nodeCount))
        {
            startNode = static_cast<BTNodeID>(btComp.debugForcedNodeId);
        }
        EvaluateTreeRecursive(startNode, btComp, trans, *btComp.sharedAsset, entity, dt);
    });
}

BTNodeState BehaviorTreeSystem::EvaluateTreeRecursive(
    BTNodeID                  nodeId,
    BehaviorTreeComponent    &btComp,
    const TransformComponent &myTrans,
    const BTAsset            &asset,
    EntityID                  entity,
    float                     dt)
{
    // ノードの存在チェック
    const BTNode &node   = asset.nodes[nodeId];
    BTNodeState   result = BTNodeState::Failure;

    // ノードの種類ごとに処理を分ける
    switch (node.type)
    {
    case BTNodeType::Selector: {        
        int runningIndex = btComp.runningNodes[nodeId];

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
            // 直前にRunningだった子ノードがあれば、そこから再開する
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
            // 成功したらRunningのしおりをリセットする
            btComp.runningNodes[nodeId] = -1;
            // その子供のActionノードのタイマーも全てリセットする
            ResetChildActionsRecursive(nodeId, btComp, asset);
        }
        break;
    }

    case BTNodeType::Condition: {
        // 強制実行中は無条件に通す。間合いの条件が付いた枝はその場の距離次第で失敗し、
        // 「実行」を押しても無反応に見えてしまう。
        if (btComp.debugForcedNodeId >= 0)
        {
            result = BTNodeState::Success;
            break;
        }

        // 条件の実装を取得して評価する。条件が無い場合は Failure。
        // 並列処理内でInstance()を読んでいるけれど
        // データの書き込みをせずに読み取り専用なので競合は起きない
        IBTCondition *cond = BTActionRegistry::Instance().GetCondition(node.actionOrConditionId);
        if (cond)
        {
            BTContext ctx;
            ctx.world       = m_world;
            ctx.entity      = entity;
            ctx.bt          = &btComp;  // ブラックボードの状態など
            ctx.myTransform = &myTrans;
            ctx.nodeId      = nodeId;
            ctx.dt          = dt;
            ctx.param       = node.param;
            result          = cond->Evaluate(ctx) ? BTNodeState::Success : BTNodeState::Failure;
        }
        else
        {
            result = BTNodeState::Failure;
        }
        break;
    }

    case BTNodeType::Action: {
        IBTAction *action = BTActionRegistry::Instance().GetAction(node.actionOrConditionId);
        if (action)
        {
            BTContext ctx;
            ctx.world       = m_world;
            ctx.entity      = entity;
            ctx.bt          = &btComp;
            ctx.myTransform = &myTrans;
            ctx.nodeId      = nodeId;
            ctx.dt          = dt;
            ctx.param       = node.param;
            result          = action->Execute(ctx);
        }
        else
        {
            result = BTNodeState::Failure;
        }

        // 終わった行動は自分のタイマーを「未実行(-1)」へ戻す。この戻しを Sequence の
        // 中だけに置いてはいけない。Selector や Decorator の直下の行動が一度完了すると
        // 0 のまま＝以後ずっと即 Success（Wait が待たない／攻撃が二度と出ない）になる。
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
        // 強制実行中は塞がない（押したのに何も起きないと見たいものが見られない）。
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
            // ※同じフレーム内に繰り返して評価するので
            // 　Conditionの距離などを図っても結果は変わらない
            // 　使い道としては乱数や確率で失敗する行動を「何度か試す」くらい

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
            // ここで子の評価を行い、結果を取得する
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
            // 子が「やり切った」ときだけ待ち時間を開始する。
            // Running で始めると技の途中からタイマー開始してしまい
            // Failure で始めると出していない技に待ちがかかる
            if (result == BTNodeState::Success) btComp.nodeTimers[nodeId] = node.param;
            break;

        case BTDecoratorType::Retry:
            break; // 上のループで結果は確定済み
        }
        break;
    }

    // 主従型の並行ノード。0番目の子が「主」で、その結果をそのまま返す。
    //   1番目以降の「従」は主が走っている間だけ動かし、結果は捨てる。
    //   しおりを持たないのが Selector / Sequence との違い。毎フレーム全部の子を
    //   評価し直すことが目的なので、途中から再開する必要がない
    //   （各子のサブツリーは自分のしおりで正しく再開する）。
    case BTNodeType::Parallel: {
        if (node.children.empty()) break;   // result は Failure のまま

        result = EvaluateTreeRecursive(node.children[0], btComp, myTrans, asset, entity, dt);

        if (result == BTNodeState::Running)
        {
            // 従を回す。戻り値は使わない。主が終わったフレームには回さない
            // （終わった技の後ろで、従の指示だけが1回余計に飛ぶのを避ける）。
            for (size_t i = 1; i < node.children.size(); ++i)
            {
                EvaluateTreeRecursive(node.children[i], btComp, myTrans, asset, entity, dt);
            }
        }
        else
        {
            // 主が終わった＝この並行区間は終わり。従の実行状態を畳む。
            //   畳まないと、従の中の行動の残り時間が 0.0 のまま残り、次に
            //   この枝へ来たとき「もう終わっている」扱いで一度も発火しない
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

        // 候補を作る。重み0以下の枝は最初から入れない（読み込み時に警告済み）。
        // 上限を超えるぶんも切り捨てる（同じく警告済み）。
        // どちらもスタックの固定長で持つ。評価はワーカースレッドで走るので、
        // ここでヒープを触るとスレッド間でアロケータを奪い合う。
        BTNodeID cand[kMaxWeightedSelectorChildren];
        float    w[kMaxWeightedSelectorChildren];
        int      count = 0;
        float    total = 0.0f;

        const int limit = (std::min)((int)node.children.size(), kMaxWeightedSelectorChildren);
        for (int i = 0; i < limit; ++i)
        {
            const float weight = asset.nodes[node.children[i]].weight;
            if (weight <= 0.0f) continue;

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
                    // しおりは node.children の添字。候補配列は詰め替えているので、
                    // 元の並びから引き直す。
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

    // デバッグ状態の保存
    // BTエディタの色分け表示
    BTDebugState dbgState = BTDebugState::None;
    if (result == BTNodeState::Success)      dbgState = BTDebugState::Success;
    else if (result == BTNodeState::Failure) dbgState = BTDebugState::Failure;
    else if (result == BTNodeState::Running) dbgState = BTDebugState::Running;
    btComp.debugNodeStates[nodeId] = dbgState;

    // デバッグ情報になんのノードを通ったか残す
    if (result == BTNodeState::Running && btComp.runningNodeId == 0xFFFF)
    {
        btComp.runningNodeId = nodeId;
    }
    return result;
}

namespace
{
    /// ActionRegistry.h の辞書から表示名を逆引きする。見つからなければ nullptr。
    const char *LookupEntryName(const ActionEntry *table, int count, ActionID id)
    {
        for (int i = 0; i < count; ++i)
        {
            if (table[i].id == id) return table[i].name;
        }
        return nullptr;
    }

    /// この枝で最初に見つかる Action の表示名。Sequence や Selector は名前を持たないので、
    /// 「その枝が何をする枝なのか」を中身の先頭アクションで示す。
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

    /// param を短く整形する。小数第1位まで（10.0 → "10.0"）。
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
    //   複合 … 今どの子で止まっているか（しおり）。動かない＝その子が Running のまま
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
            std::snprintf(buf, sizeof(buf), "  しおり %d/%d",
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
        // 評価エンジンはボス固有の概念を持たない方針なので、後始末はデバッグUIだけで行う。
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

    // 木を凍らせる。移動中に止めるとボスが歩き続けるので、行動要求も併せて切る。
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

        // lastLogActionId から行動名を逆引きする。
        std::string actionName = "Idle";
        for (int i = 0; i < g_ActionRegistryCount; ++i)
        {
            if (g_ActionRegistry[i].id == btComp->lastLogActionId)
            {
                actionName = g_ActionRegistry[i].name;
                break;
            }
        }
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Current Action: %s", actionName.c_str());

        ImGui::Text("Distance To Target: %.2fm", btComp->blackboard.distanceToTarget);

        // 角度と接近速度は符号に意味があるので、読み方を横に出す（右か左か、詰めか引きか）。
        ImGui::Text("Angle To Target  : %+.1f deg  [%s]", btComp->blackboard.angleToTarget,
                    (btComp->blackboard.angleToTarget >= 0.0f) ? "右" : "左");
        ImGui::Text("Closing Speed    : %+.2f m/s  [%s]", btComp->blackboard.targetClosingSpeed,
                    (btComp->blackboard.targetClosingSpeed >= 0.0f) ? "接近" : "後退");

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
