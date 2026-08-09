/// @file  StateTreeNode.h
/// @brief StateTree を構成する「タスク」と「条件」の基底インターフェース定義。
///
/// 設計の核心:
///   - ITask / ICondition は「種別ごとに1インスタンス」のステートレス共有オブジェクト。
///     個体差（タイマー値など）が要る場合は、専用の scratch 領域ではなく
///     ゲーム側の Component（例: DroneComponent.stateTimer）に持たせる方針。
///   - virtual 呼び出しは「個体数」ではなく「種別数（数十個）」分しか起きないので
///     キャッシュミスの影響は無視できる。ホットなのは StateTreeSystem の ForEachParallel。
///   - ここでは Component や World への依存を一切持たない。
///     World への実際のアクセスは ExecContext 越しに行う。
#pragma once
#include <cstdint>

// World / EntityID はポインタ参照のみなので前方宣言で十分
namespace CCL::ECS { class World; }
namespace CCL::ECS        { using EntityID = unsigned long long; }


namespace CCL::AI
{
    // ====================================================================
    // ExecContext
    // ====================================================================

    /// @brief タスク・条件が実行時に受け取る「全情報のカバン」。
    ///
    /// StateTreeSystem が毎フレーム組み立てて ITask::Tick / ICondition::Evaluate に渡す。
    /// ExecContext 越しにのみ World / Component にアクセスすることで、
    /// ノード実装が System の依存グラフを汚染するのを防ぐ。
    ///
    /// メモリレイアウト:
    ///   params    → StateTreeAsset::paramBlob 内の該当オフセット (読み取り専用)
    ///   個体別の可変状態が必要なら world/entity からゲーム側 Component を引いて書く。
    struct ExecContext
    {
        CCL::ECS::World* world    = nullptr;
        CCL::ECS::EntityID     entity   = 0;

        // このノードのパラメータ（JSONから焼き込まれた定数。読み取り専用）
        const void*  params   = nullptr;

        float dt = 0.0f;
    };


    // ====================================================================
    // RunStatus
    // ====================================================================

    /// @brief タスクの1フレーム実行結果。
    ///
    ///   Running   : まだ実行中。次フレームも Tick を呼び続ける。
    ///   Succeeded : 目標達成。StateTree は次の状態/遷移を評価する。
    ///   Failed    : 失敗。StateTree はフォールバック遷移を評価する。
    enum class RunStatus : uint8_t
    {
        Running   = 0,
        Succeeded = 1,
        Failed    = 2,
    };


    // ====================================================================
    // ITask
    // ====================================================================

    /// @brief 「状態に滞在している間、毎フレーム実行されるロジック」の基底。
    /// @details ITaskは全員で１個を共有する設計、敵ごとに一つITaskではなく
    ///          敵全員が１個のITaskを共有する。
    struct ITask
    {
        virtual ~ITask() = default;

        /// @brief この状態に入った瞬間に1度だけ呼ばれる。
        ///        タイマーリセット・アニメーション開始要求などに使用。
        virtual void EnterState(ExecContext& /*ctx*/) {}

        /// @brief 毎フレーム呼ばれるメインロジック。
        /// @return Running / Succeeded / Failed
        virtual RunStatus Tick(ExecContext& ctx) = 0;

        /// @brief この状態から出る瞬間に1度だけ呼ばれる。
        ///        アニメーション停止要求などのクリーンアップに使用。
        virtual void ExitState(ExecContext& /*ctx*/) {}
    };


    // ====================================================================
    // ICondition
    // ====================================================================

    /// @brief 「遷移の可否」または「状態への入場可否」を1フレームで判定する基底。
    ///
    /// 条件はステートレス（インスタンスデータを持たない）が基本。
    /// タイマー条件のように状態が必要な場合は ITask + Succeeded で代替する設計を検討すること。
    ///
    /// @code
    ///   struct IsTargetInRangeCondition : public ICondition {
    ///       struct Params { float range = 8.0f; };
    ///       bool Evaluate(ExecContext& ctx) override {
    ///           auto* p = static_cast<const Params*>(ctx.params);
    ///           auto* perc = ctx.world->GetComponent<PerceptionComponent>(ctx.entity);
    ///           return perc && perc->distance <= p->range;
    ///       }
    ///   };
    /// @endcode
    struct ICondition
    {
        virtual ~ICondition() = default;

        /// @brief 条件を評価し true (満足) / false (不満足) を返す。
        virtual bool Evaluate(ExecContext& ctx) = 0;
    };

} // namespace CCL::AI
