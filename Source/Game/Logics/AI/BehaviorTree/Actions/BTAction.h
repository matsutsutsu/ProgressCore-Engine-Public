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
    // アクション/条件が受け取る情報。どのエンティティのBTでも使える汎用データだけ
    struct BTContext
    {
        CCL::ECS::World   *world  = nullptr; // 評価中の World（所有しない）
        CCL::ECS::EntityID entity = 0;       // BT を動かしているエンティティ

        BehaviorTreeComponent *bt = nullptr; // そのエンティティの実行状態（所有しない）

        const TransformComponent *myTransform = nullptr; // そのエンティティの姿勢（所有しない）

        BTNodeID nodeId = 0;    // 評価中のノード。nodeTimers[nodeId] を引くのに使う
        float    dt     = 0.0f; // フレームの経過時間（秒）

        float param = 0.0f; // ノードの数値パラメータ。待機の秒数や硬直など行動の引数
    };

    // 型の並びを TypeID の配列にする。GetExtraWriteTypes / GetExtraReadTypes の戻り値を作るのに使う
    template <typename... Ts>
    inline std::vector<CCL::ECS::TypeID> TypeIDsOf()
    {
        return {CCL::ECS::TypeInfo<Ts>::ID()...};
    }

    /**
     * @brief BT の葉になる行動の基底。ActionID ごとに1つだけ作られ、全エンティティで共有される。
     * @note 共有されるので、メンバに個体の状態を持たせない。状態は BTContext::bt の nodeTimers などに置く。
     */
    struct IBTAction
    {
        virtual ~IBTAction() = default;

        // 1フレーム分の行動を進める。続くなら Running を返す。ワーカースレッドから並列に呼ばれる
        virtual BTNodeState Execute(BTContext &ctx) = 0;

        /**
         * @brief 途中で打ち切られてよいか。false を返すと、その枝への割り込みは見送られる。
         * @note 拒否するのは、要求を取り下げた瞬間に動きが崩れる技だけ（空中の跳躍など）。
         */
        virtual bool CanAbort(BTContext &ctx) const { return true; }

        /**
         * @brief 打ち切られたときの後始末。出したままの要求を取り下げる。
         * @note 打ち切りでは Execute が最後まで走らない。完了時の取り下げを Execute に書いた技は、ここにも書く。
         */
        virtual void OnAbort(BTContext &ctx) {}

        /**
         * @brief world から GetComponent<T>() で書き換える Component を申告する（TypeIDsOf で作る）。
         * @note 書き忘れてもエラーは出ず、スケジューラが並列に走らせてデータ競合になる。GetComponent<T>() を足したら必ずここも足す。
         */
        virtual std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const { return {}; }

        // world から GetComponent<T>() で読む Component を申告する
        virtual std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const { return {}; }
    };

    /**
     * @brief BT の条件ノードの基底。ActionID ごとに1つだけ作られ、全エンティティで共有される。
     * @note 割り込みの判定でも呼ばれるので、評価しただけで状態が変わる書き方をしない。
     */
    struct IBTCondition
    {
        virtual ~IBTCondition() = default;

        // 条件を評価する。閾値は ctx.param。ワーカースレッドから並列に呼ばれる
        virtual bool Evaluate(BTContext &ctx) = 0;

        virtual std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const { return {}; } // IBTAction の同名関数と同じ
        virtual std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const { return {}; }  // IBTAction の同名関数と同じ
    };

} // namespace CCL::AI::BT
