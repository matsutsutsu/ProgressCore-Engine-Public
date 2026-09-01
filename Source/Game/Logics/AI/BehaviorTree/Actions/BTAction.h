// BehaviorTree のアクション・条件の基底。
//
// 汎用化（ボス固有を排除）
//   BTContext からボス固有の command(BossCommand) / myAction(BossAction) を外した。
//   これらが必要なアクション（ボス用）は Execute の中で
//   ctx.world->GetComponent<BossCommandComponent>(ctx.entity) と自分で取りに行く。
//   → BTエンジンとContextが「ボス」を一切知らなくなり、ドローンにも流用できる。
#pragma once
#include <cstdint>
#include <vector>

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"        // BTNodeState, ActionID, BTNodeID
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"  // BehaviorTreeComponent
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"   // アクセス申告（TypeInfo<T>::ID）

// 前方宣言
namespace CCL::ECS { class World; }
struct TransformComponent;


namespace CCL::AI::BT
{
    // ====================================================================
    // BTContext — アクション/条件が受け取る情報のカバン（汎用のみ）
    // ====================================================================
    // どのエンティティのBTでも使える汎用データだけを持つ。
    //        ボス固有のコンポーネントは world/entity から自分で取得する。
    struct BTContext
    {
        CCL::ECS::World* world  = nullptr;
        CCL::ECS::EntityID     entity = 0;

        // --- AI状態（nodeTimers / blackboard / lastLogActionId を読み書き）---
        BehaviorTreeComponent* bt = nullptr;

        // --- 読み取り専用データ（どのAIも持ちうる汎用コンポーネント）---
        const TransformComponent* myTransform = nullptr;

        // --- ノード情報 ---
        BTNodeID nodeId = 0;   // nodeTimers[nodeId] を引くのに使う
        float    dt     = 0.0f;

        // このノードに設定された数値パラメータ（BTNode::param）。
        //        待機の秒数や近接の硬直など、行動の引数をここから受け取る。
        //        使わない行動は無視してよい（既定 0）。
        float    param  = 0.0f;
    };


    // ====================================================================
    // アクセス申告 — スケジューラへ「BTが何を触るか」を伝えるための仕組み
    // ====================================================================
    // なぜ要るか（データ競合の実例）:
    //   BTContext を汎用に保つ代わりに、各アクションは必要なコンポーネントを
    //   ctx.world->GetComponent<T>() で自分で取りに行く。この書き込みは
    //   BehaviorTreeSystem の Read<>/Write<> 宣言に一切現れないので、
    //   TaskScheduler からは「BehaviorTreeSystem は BehaviorTreeComponent しか
    //   触らない」ように見える。
    //   実際には TriggeredMotionAction が AnimParametersComponent を書いており、
    //   同じ L02_Update の AnimControllerSystem も同じコンポーネントを書く。
    //   宣言が重ならない＝競合なしと判定され、両者は同じバッチ（＝並列）に入る。
    //   結果、同一の std::vector に push_back と erase が同時に走っていた。
    //
    // 解決:
    //   アクション自身に「自分が触る型」を申告させ、BTActionRegistry が
    //   登録時に集約し、BehaviorTreeSystem がそれを自分の申告へ足して返す。
    //   敵の種類を足しても、そのアクションが申告していればスケジューラは自動で追従する
    //   （BTエンジンのヘッダは相変わらずボスもドローンも知らないまま）。
    //
    // @note ここに書き忘れたコンポーネントは、静かにデータ競合になる。
    //       ctx.world->GetComponent<T>() を新しく書いたら必ず1行足すこと。
    template <typename... Ts>
    inline std::vector<CCL::ECS::TypeID> TypeIDsOf()
    {
        return { CCL::ECS::TypeInfo<Ts>::ID()... };
    }

    // ====================================================================
    // IBTAction — 行動の基底
    // ====================================================================
    struct IBTAction
    {
        virtual ~IBTAction() = default;
        // 1フレームの行動。Success / Failure / Running を返す。
        virtual BTNodeState Execute(BTContext& ctx) = 0;

        // ctx.world->GetComponent<T>() で書き換えるコンポーネント（上の説明を参照）。
        virtual std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const { return {}; }
        // 同じく、読むだけのコンポーネント。
        virtual std::vector<CCL::ECS::TypeID> GetExtraReadTypes()  const { return {}; }
    };

    // ====================================================================
    // IBTCondition — 条件の基底
    // ====================================================================
    struct IBTCondition
    {
        virtual ~IBTCondition() = default;
        virtual bool Evaluate(BTContext& ctx) = 0;

        virtual std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const { return {}; }
        virtual std::vector<CCL::ECS::TypeID> GetExtraReadTypes()  const { return {}; }
    };

} // namespace CCL::AI::BT
