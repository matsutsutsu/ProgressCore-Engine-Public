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

using namespace CCL::ECS;
using namespace CCL::AI;
using namespace CCL::AI::BT;

// ---------------------------------------------------------------------------
// 乱数の種
// ---------------------------------------------------------------------------

namespace
{
    // 抽選の乱数の種を、個体ごとに1回だけ決める（entity から作る）
    void SeedRandomIfNeeded(BehaviorTreeComponent &btComp, CCL::ECS::EntityID entity)
    {
        // 乱数の状態は個体ごとに持つ。評価が並列に走るので、1つを共有するとデータ競合になる。
        // これだけでは同じ試合を再現できない（dt が可変で、抽選を引く回数が毎回変わる）。
        if (btComp.rngState != 0) return;

        // 0 は「未初期化」の印なので必ず非ゼロにする
        btComp.rngState = (static_cast<uint32_t>(entity) * 2654435761u) ^ 0x9E3779B9u;
        if (btComp.rngState == 0) btComp.rngState = 0x6D2B79F5u;
    }
}

// ---------------------------------------------------------------------------
// 初期化と資産の同期
// ---------------------------------------------------------------------------

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
    // 行動・条件を足すときは2か所に書く：
    //   1. 辞書（BTActionIds.h）    … 名前と ID の一覧。エディタの選択肢はここから作られる
    //   2. 登録関数（Register〜()） … ID と実際に動く関数を結びつける
    // 2 を書き忘れると、エディタでは選べるのに木に置いても何も起きない
    // （行動は Failure、条件は常に false）。エラーも出ないので原因を探しにくい。
    // そこで起動時に辞書を1件ずつ見て、2 に無いものをエラーログで知らせる。
    // ERROR なので Shipping でも出る（WARN/INFO は Shipping では消える）。
    const auto &reg = BTActionRegistry::Instance();

    // 辞書の先頭（ID 0）は「なし」の印で、実装を持たないので調べない
    for (int i = 0; i < g_ActionRegistryCount; ++i)
    {
        const ActionEntry &e = g_ActionRegistry[i];
        if (e.id != 0 && !reg.HasAction(e.id))
        {
            CCL_LOG_ERROR(LogCategory::AI, "[BT] 行動 \"%s\" が未登録（登録関数への追加漏れ）", e.key);
        }
    }
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
    {
        const ActionEntry &e = g_ConditionRegistry[i];
        if (e.id != 0 && !reg.HasCondition(e.id))
        {
            CCL_LOG_ERROR(LogCategory::AI, "[BT] 条件 \"%s\" が未登録（登録関数への追加漏れ）", e.key);
        }
    }
}

std::vector<CCL::ECS::TypeID> BehaviorTreeSystem::GetReadTypes() const
{
    // アクションが GetComponent<T>() で行う読み書きはテンプレート引数に現れない。足さないとスケジューラが
    // 「BehaviorTreeComponent しか触らない」と判断して、同じものを書く別の System と並列に走らせてしまう。
    // 順序を名前で宣言（GetRunBefore など）せず型で申告するのは、名前は1対1にしか効かないのに対し、型はそれを触る全員に効くから。
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
        btComp.debugScores.assign(nodeCount, BTScoreDebug{});
        btComp.nodeTimers.assign(nodeCount, -1.0f);
        btComp.runningNodes.assign(nodeCount, -1);
        btComp.repeatTimers.assign(nodeCount, 0.0f);
        btComp.cooldownTimers.assign(nodeCount, 0.0f);
        btComp.debugPickCount = 0; // 記録のノードIDは前の木のもの
        btComp.runningNodeId  = 0xFFFF;
    });
}

// ---------------------------------------------------------------------------
// 毎ステップの入口
// ---------------------------------------------------------------------------

void BehaviorTreeSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. 各個体へ木を配る（直列）
    // 2. 木を評価する（並列）

    if (!m_world->HasResource<GameFlowState>()) return;

    // カットシーン中と決着後（リザルト）はAIの思考を止める
    const auto &flow      = m_world->GetResource<GameFlowState>();
    const bool  cinematic = flow.cinematic || flow.matchOver;

    // ---------------------------------------------------------
    // 1. 木を配る
    // ---------------------------------------------------------
    SyncAssets();

    // ---------------------------------------------------------
    // 2. 評価
    // ---------------------------------------------------------
    ForEachWithIDParallel([this, cinematic](EntityID                  entity,
                                            BehaviorTreeComponent    &btComp,
                                            const TransformComponent &trans,
                                            const TimeStateComponent &time) {
        float dt = time.localDt;

        // BTのデータが無いか、木のノードが空なら何もしない
        if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty()) return;

        // BTデータのノード数取得
        const size_t nodeCount = btComp.sharedAsset->nodes.size();

        // アセットのノード数とBTの実行状態の配列の長さが合わない場合は何もしない
        if (btComp.nodeTimers.size() != nodeCount) return;

        // 一時停止中（強制実行を除く）とカットシーン中は評価しない
        const bool forcing = (btComp.debugForcedNodeId >= 0);
        if ((btComp.debugPaused && !forcing) || cinematic)
        {
            //
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

        // スコア選択の繰り返しの減点を減らす（0 で減点が消える）
        for (float &t : btComp.repeatTimers)
        {
            t = (std::max)(0.0f, t - dt);
        }

        // スコア選択の子のクールダウンを減らす（0 で候補に戻る）
        for (float &t : btComp.cooldownTimers)
        {
            t = (std::max)(0.0f, t - dt);
        }

        // 技選択の記録の「何秒前」に使う時計を進める
        btComp.debugClock += dt;

        // Runningノードをクリア
        btComp.runningNodeId = 0xFFFF;

        // 抽選に使う乱数のシード値は個体ごとに1回だけ決める、２回目以降はスキップ
        SeedRandomIfNeeded(btComp, entity);

        // 前フレームの評価結果（BT ウィンドウの色分け）を消す。今フレームの評価で埋め直す
        std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);

        // デバッグの強制実行があればその枝から、無ければルート(0)から評価する
        const BTNodeID startNode = ApplyDebugForcing(btComp, nodeCount);

        // 木を再帰的に評価する。結果は btComp.debugNodeStates に入る
        EvaluateTreeRecursive(startNode, btComp, trans, *btComp.sharedAsset, entity, dt);
    });
}

REGISTER_LOGIC_SYSTEM(BehaviorTreeSystem, Priority::LogicStage::L02_Update);
