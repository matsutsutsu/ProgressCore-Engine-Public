#pragma once
// ===========================================================================
// 概要: 生ポインタ T* の代わりに保持できる、安全なコンポーネント参照 ComponentHandle<T>
//
// 責務:
//   - (World*, EntityID) を保持し、アクセスのたびに最新のアドレスを引き直す
//   - Entity の生存・コンポーネント所持の確認（IsValid / bool 変換）
//
// 備考: Chunk 内のコンポーネントは Entity の追加・削除や Archetype 遷移で
//       メモリ上の位置が変わる。生ポインタを持ち続けると構造変更後に
//       ダングリングしてクラッシュするため、長く持つ参照はこちらを使う
// ===========================================================================

#include <cassert>

#include "../Common/CCL_Types.h"

namespace CCL::ECS
{
    // 前方宣言（World の完全定義は実装側で必要）
    class World;

    // コンポーネントを間接に指すハンドル。(World*, EntityID) だけを持ち、使うたびに GetComponent で引き直す
    // 構造が変わっても今のアドレスを返し、Entity が消えれば nullptr になる。フレームをまたいで持つときに使う
    template <typename T>
    class ComponentHandle
    {
    public:
        // 無効ハンドルを構築する
        ComponentHandle()
            : m_world(nullptr), m_id(InvalidEntityID)
        {
        }

        // 有効ハンドルを構築する（通常は World::GetHandle 経由で生成）
        ComponentHandle(World *world, EntityID id)
            : m_world(world), m_id(id)
        {
        }

        // Entity が生きていて、T を持っているか
        bool IsValid() const;

        // T* を返す。無効なら nullptr
        T *Get() const;

        // 無効なハンドルに触ると assert で止まる（宙に浮いたポインタを開発中に見つける）
        T *operator->() const
        {
            T *ptr = Get();
            assert(ptr && "Accessing invalid or destroyed component! (Handle expired)");
            return ptr;
        }

        // `if (handle) { ... }` と書けるようにする
        explicit operator bool() const { return IsValid(); }

        // 同じ World 上の同じ Entity を指すかで一致判定
        bool operator==(const ComponentHandle<T> &other) const
        {
            return m_world == other.m_world && m_id == other.m_id;
        }

    private:
        World   *m_world; // 所有しない
        EntityID m_id;
    };

} // namespace CCL::ECS
