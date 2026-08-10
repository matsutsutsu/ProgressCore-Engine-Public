#pragma once
#include <cstdint>

// 前方宣言
namespace CCL::ECS { class World; }
namespace CCL::ECS { using EntityID = unsigned long long; }

/// @brief StateTreeのノードの基本情報と構造体定義
namespace CCL::AI
{

    /**
     * @brief StateTreeの処理中に渡されるコンテキスト情報
     * @details StateTreeSystemのノード処理中に、各ノードに渡される実行情報をまとめた構造体
     */
    struct ExecContext
    {
        CCL::ECS::World* world    = nullptr;
        CCL::ECS::EntityID     entity   = 0;

        // このノードのパラメータ（JSONから焼き込まれた定数。読み取り専用）
        const void*  params   = nullptr;

        float dt = 0.0f;
    };


    /**
     * @brief ノードの実行結果を表す列挙型
     * @details Running: 実行中, Succeeded: 成功, Failed:失敗
     */
    enum class RunStatus : uint8_t
    {
        Running   = 0,
        Succeeded = 1,
        Failed    = 2,
    };


    /**
     * @brief StateTreeの処理を表すノード構造体
     * @details このタスクが実行された時何をするかの処理内容
     */
    struct ITask
    {
        virtual ~ITask() = default;

        /**
         * @brief この状態に入る瞬間に1度だけ呼ばれる
         * @details ExecContextを使わないなら空でも良い
         */
        virtual void EnterState(ExecContext& /*ctx*/) {}

        /**
         * @brief 毎フレーム呼ばれる処理
         * @details このTaskが実行されるときに毎フレーム呼ばれる処理を定義する
         * @return RunStates： Running: 実行中, Succeeded: 成功, Failed:失敗
         */
        virtual RunStatus Tick(ExecContext& ctx) = 0;

        /**
         * @brief この状態から抜ける瞬間に1度だけ呼ばれる
         * @details ExecContextを使わないなら空でも良い
         */
        virtual void ExitState(ExecContext& /*ctx*/) {}
    };


    /**
     * @brief 条件を表すノード構造体
     * @details この条件が満たされるかどうかを評価する処理内容
     */ 
    struct ICondition
    {
        virtual ~ICondition() = default;

        /**
         * @brief 条件を評価し true / false を返す
         */
        virtual bool Evaluate(ExecContext& ctx) = 0;
    };

} 
