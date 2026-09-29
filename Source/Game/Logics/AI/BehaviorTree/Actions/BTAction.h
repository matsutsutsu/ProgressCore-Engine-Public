#pragma once
// ===========================================================================
// 概要: BT のアクションと条件の基底クラスと、それらに渡す BTContext。
//
// 責務:
//   - アクションの Execute / CanAbort / OnAbort と、条件の Evaluate を定義する
//   - world から直接触る Component を、スケジューラへ申告する口を用意する
//
// 備考: BTContext はボス固有の Component を持たない。必要なアクションは Execute の中で
//       ctx.world->GetComponent<T>(ctx.entity) で自分で取る。
//       そのため BT の評価の処理はボスを知らず、他の敵にも使える。
// ===========================================================================
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

    // アクセスの申告。スケジューラへ、BT が何を触るかを伝える。
    //
    // アクションが GetComponent<T>() で行う読み書きは BehaviorTreeSystem の
    // Read<>/Write<> 宣言に現れず、スケジューラからは見えない。申告が無いと、たとえば
    // TriggeredMotionAction と AnimControllerSystem が同じ AnimParametersComponent を書くのに
    // 「競合なし」と判定され、同じ vector へ push_back と erase が同時に入る。
    // そこでアクション自身に申告させ、レジストリがまとめて BehaviorTreeSystem の申告に足す。
    //
    // ここに書き忘れた Component は、何のエラーも出ずにデータ競合になる。
    // GetComponent<T>() を新しく書いたら必ず1行足すこと。
    template <typename... Ts>
    inline std::vector<CCL::ECS::TypeID> TypeIDsOf()
    {
        return {CCL::ECS::TypeInfo<Ts>::ID()...};
    }

    struct IBTAction
    {
        virtual ~IBTAction() = default;
        // 1フレーム分の行動。Success / Failure / Running を返す。
        virtual BTNodeState Execute(BTContext &ctx) = 0;

        /**
         * @brief 途中で打ち切られてよいか。false を返すと、その枝への割り込みは見送られる。
         *
         * 拒否するのは、要求を取り下げた瞬間に動きが崩れる技だけ。跳躍がその例で、空中で
         * 要求が下りると体を動かす側が待機になり、放物線の途中から重力で落ちる。
         */
        virtual bool CanAbort(BTContext &ctx) const { return true; }

        /**
         * @brief 打ち切られたときの後始末。出したままの要求を取り下げるのに使う。
         *
         * 完了時の後始末を Execute の中に書いている技は、ここにも同じ取り下げが要る。
         * 打ち切りでは Execute が最後まで走らないので、完了時の後始末を通らない。
         */
        virtual void OnAbort(BTContext &ctx) {}

        // GetComponent<T>() で書き換える／読む Component（上の説明を参照）。
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
