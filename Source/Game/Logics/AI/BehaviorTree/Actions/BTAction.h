// BehaviorTree のアクション・条件の基底。
//
// BTContext はボス固有のコンポーネントを持たない。必要なアクションは Execute の中で
// ctx.world->GetComponent<T>(ctx.entity) と自分で取りに行く。
// おかげでBTエンジンは「ボス」を一切知らず、ドローンにも流用できる。
#pragma once
#include <cstdint>
#include <vector>

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"       // BTNodeState, ActionID, BTNodeID
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h" // アクセス申告（TypeInfo<T>::ID）

namespace CCL::ECS
{
    class World;
}
struct TransformComponent;

namespace CCL::AI::BT
{
    // アクション/条件が受け取る情報。どのエンティティのBTでも使える汎用データだけ。
    struct BTContext
    {
        CCL::ECS::World   *world  = nullptr;
        CCL::ECS::EntityID entity = 0;

        BehaviorTreeComponent *bt = nullptr;

        const TransformComponent *myTransform = nullptr;

        BTNodeID nodeId = 0; // nodeTimers[nodeId] を引くのに使う
        float    dt     = 0.0f;

        // ノードの数値パラメータ。待機の秒数や硬直など行動の引数（既定 0）。
        float param = 0.0f;
    };

    // アクセス申告 — スケジューラへ「BTが何を触るか」を伝える。
    //
    // アクションが GetComponent<T>() で行う読み書きは BehaviorTreeSystem の
    // Read<>/Write<> 宣言に現れず、スケジューラからは見えない。実際に
    // TriggeredMotionAction と AnimControllerSystem が同じ AnimParametersComponent を
    // 書いていたのに「競合なし」と判定され、同一の vector へ push_back と erase が
    // 同時に入っていた。アクション自身に申告させ、レジストリが集約して足す。
    //
    // ここに書き忘れたコンポーネントは静かにデータ競合になる。
    // GetComponent<T>() を新しく書いたら必ず1行足すこと。
    template <typename... Ts>
    inline std::vector<CCL::ECS::TypeID> TypeIDsOf()
    {
        return {CCL::ECS::TypeInfo<Ts>::ID()...};
    }

    struct IBTAction
    {
        virtual ~IBTAction() = default;
        /// 1フレームの行動。Success / Failure / Running を返す。
        virtual BTNodeState Execute(BTContext &ctx) = 0;

        /// GetComponent<T>() で書き換える／読むコンポーネント（上の説明を参照）。
        virtual std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const { return {}; }
        virtual std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const { return {}; }
    };

    struct IBTCondition
    {
        virtual ~IBTCondition()               = default;
        virtual bool Evaluate(BTContext &ctx) = 0;

        virtual std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const { return {}; }
        virtual std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const { return {}; }
    };

} // namespace CCL::AI::BT
