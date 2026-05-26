/**
 * @file   CCL_PendingOps.h
 * @brief  System実行中の構造変更要求を後でまとめて処理するための予約データ
 *
 * @details
 *  System実行中に Entity生成/削除/コンポーネント追加削除 を即時実行すると、
 *  ループ中のChunk構造が変わってイテレータが無効化される問題があります。
 *  これを避けるため、要求だけを PendingOp として記録しておき、
 *  フレームの終わりにまとめて適用します。
 *
 *  以下を提供します:
 *   - PendingOpKind : 操作の種別 enum
 *   - PendingOp     : 1つの予約操作を表す構造体
 *   - PendingOps    : 予約のリスト(コンテナ)
 *
 *  【依存】
 *   - CCL_Archetype.h, CCL_TypeData.h
 *   - \<vector\>, \<functional\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "../Common/CCL_Archetype.h"

#include <vector>
#include <functional>


namespace CCL::ECS::Core
{
    using namespace CCL::ECS;

    // =========================================================================
    //  PendingOpKind
    // =========================================================================

    /**
     * @enum PendingOpKind
     * @brief 構造変更要求の種別
     */
    enum class PendingOpKind
    {
        None             = 0, ///< 未設定
        Spawn,                ///< 新規Entity生成
        Destroy,              ///< Entity削除
        AddComponent,         ///< Component追加
        RemoveComponent,      ///< Component削除
        PatchComponent       ///< 既存Componentの部分修正
    };


    // =========================================================================
    //  PendingOp
    // =========================================================================

    /**
     * @struct PendingOp
     * @brief 1つの「構造変更要求」を表現するデータカプセル
     *
     * @details
     *  Worldが System実行中に蓄積し、ScrutinyAndApply 時に ChunkManager へ
     *  まとめて渡されます。種別(kind)によって使用するメンバが変わります:
     *
     *  - Spawn          : spawnArchetype を使用
     *  - Destroy        : entity のみ使用
     *  - AddComponent   : type, typeSize, factory または data + 各関数ポインタ
     *  - RemoveComponent: type のみ
     *  - PatchComponent : type, patcher を使用
     */
    struct PendingOp
    {
        PendingOpKind kind        = PendingOpKind::None;
        EntityID      entity      = InvalidEntityID;

        // -- AddComponent / RemoveComponent / PatchComponent --
        TypeID        type        = InvalidTypeID;
        std::size_t   typeSize    = 0;

        /// @brief AddComponent用: コピーするための初期データ(FrameAllocator上)
        void*         data        = nullptr;

        /// @brief AddComponent用: data の代わりにラムダから直接構築
        std::function<void(void*)> factory  = nullptr;

        /// @brief PatchComponent用: 既存ポインタを書き換えるラムダ
        std::function<void(void*)> patcher  = nullptr;

        // -- 型操作関数群(ChunkManagerに伝達する必要あり) --
        Destructor                 destructor;
        TypeData::ConstructFunc    constructor = nullptr;
        TypeData::AssignFunc       assigner    = nullptr;
        TypeData::MoveFunc         mover       = nullptr;

        /// @brief デバッグ用型名
        const char*                name        = nullptr;

        /**
         * @brief Spawn 時のArchetype
         * @details FrameAllocatorのライフタイム問題を回避するため、値で保持。
         */
        Archetype                  spawnArchetype;
    };


    // =========================================================================
    //  PendingOps
    // =========================================================================

    /**
     * @class PendingOps
     * @brief 1フレーム分の構造変更予約を蓄える簡易コンテナ
     *
     * @details
     *  内部にスレッド同期は持ちません。Worldが System実行中に書き込み、
     *  ScrutinyAndApply 時に ChunkManager が読み出す想定です。
     *  並列Spawn等を行う場合は Worldレベルでロックしてください。
     */
    class PendingOps
    {
    public:
        PendingOps() = default;
        ~PendingOps() { Clear(); }

        // コピー禁止(ヒープを参照する可能性あり)
        PendingOps(const PendingOps&)            = delete;
        PendingOps& operator=(const PendingOps&) = delete;

        /// @brief 予約を追加
        void Add(const PendingOp& op) { _ops.push_back(op); }

        /// @brief 予約を追加(ムーブ版)
        void Add(PendingOp&& op)      { _ops.emplace_back(std::move(op)); }

        /// @brief 予約リストへのアクセス
        std::vector<PendingOp>&       Ops()       noexcept { return _ops; }
        const std::vector<PendingOp>& Ops() const noexcept { return _ops; }

        /**
         * @brief 全予約をクリアする
         * @details AddComponentに紐づく一時データがあれば、デストラクタを呼びます。
         *          FrameAllocator上のメモリならアロケータ側でReset()される想定。
         */
        void Clear()
        {
            for (auto& op : _ops) {
                if (op.kind == PendingOpKind::AddComponent && op.data) {
                    op.destructor.Destruct(op.data);
                }
                op.data = nullptr;
            }
            _ops.clear();
        }

        /// @brief 予約がないか
        bool Empty() const noexcept { return _ops.empty(); }

        /// @brief 予約件数
        std::size_t Size() const noexcept { return _ops.size(); }

    private:
        std::vector<PendingOp> _ops;
    };

} // namespace CCL::ECS::Core
