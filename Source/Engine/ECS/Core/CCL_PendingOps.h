#pragma once
// ===========================================================================
// 概要: System 実行中に出た構造変更要求（生成・削除・コンポーネント追加/削除/修正）の
//       遅延処理リスト
//
// 責務:
//   - 構造変更要求を PendingOp として記録する
//   - 未適用のまま破棄するとき、AddComponent 用に確保した初期データを解放する
//
// 備考: System の更新中に Chunk の構造を変えると、走査中の配列が壊れる
//       そのため要求だけ溜めておき、World::ScrutinyAndApply で安全なタイミングに一括適用する
// ===========================================================================

#include "Engine/ECS/Common/CCL_Types.h"
#include <vector>
#include <memory>
#include <functional>

namespace CCL::ECS
{
    enum class PendingOpKind
    {
        Spawn,
        Destroy,
        AddComponent,
        RemoveComponent,
        PatchComponent, // 既存コンポーネントの一部書き換え

        None
    };

    // 構造変更要求1件ぶん。kind によって使うフィールドが変わる
    struct PendingOp
    {
        PendingOpKind kind   = PendingOpKind::None; // 実行する操作の種類
        EntityID      entity = 0;                   // 操作対象の Entity

        TypeID type     = 0; // 操作対象のコンポーネントの TypeID
        size_t typeSize = 0; // 操作対象のコンポーネントのサイズ（byte）

        // AddComponent の初期データ（ヒープ上。所有する。PendingOps::Clear で解放）
        // factory を使う場合など、nullptr のこともある
        void      *data = nullptr;
        Destructor destructor; // data を破棄するための型消去デストラクタ

        // Emplace 用の生成関数。AddComponent で渡されたラムダを ChunkManager まで運ぶ
        std::function<void(void *)> factory = nullptr;

        // Patch 用。既存コンポーネントのポインタを受け取り、中身を書き換えるラムダ
        std::function<void(void *)> patcher = nullptr;

        // コピー・移動・代入用の関数ポインタ
        // これらを ChunkManager まで運ばないと memcpy で代用されてしまい、
        // std::vector などを持つ型でクラッシュする
        TypeData::ConstructFunc constructor = nullptr; // コピー構築（placement new）
        TypeData::AssignFunc    assigner    = nullptr; // 代入（operator=）
        TypeData::MoveFunc      mover       = nullptr; // ムーブ構築

        const char *name = nullptr; // デバッグ用の型名

        // Spawn 時の Archetype を値で保持する
        //   ポインタで持ってはいけない。FrameAllocator 上の Archetype を指すと
        //   Reset() 後にダングリングする
        Archetype spawnArchetype;
    };

    // 構造変更要求を溜めておくリスト
    class PendingOps
    {
    public:
        PendingOps() = default;
        ~PendingOps() { Clear(); }

        void Add(const PendingOp &op)
        {
            m_ops.push_back(op);
        }

        std::vector<PendingOp> &Ops() { return m_ops; }

        // 全要求を破棄する。AddComponent 用に確保した初期データ（op.data）もここで解放する
        void Clear()
        {
            for (auto &op : m_ops)
            {
                if (op.data == nullptr) continue;

                if (op.kind == PendingOpKind::AddComponent)
                {
                    op.destructor.Destruct(op.data);
                }

                op.data = nullptr;
            }
            m_ops.clear();
        }

        bool Empty() const { return m_ops.empty(); }

    private:
        std::vector<PendingOp> m_ops;
    };
} // namespace CCL::ECS
