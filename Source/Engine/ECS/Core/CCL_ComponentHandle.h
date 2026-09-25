#pragma once
// ===========================================================================
// 概要: 生ポインタ T* の代わりに保持できる、安全なコンポーネント参照 ComponentHandle<T>。
//
// 責務:
//   - (World*, EntityID) を保持し、アクセスのたびに最新のアドレスを引き直す
//   - Entity の生存・コンポーネント所持の確認（IsValid / bool 変換）
//
// 備考: Chunk 内のコンポーネントは Entity の追加・削除や Archetype 遷移で
//       メモリ上の位置が変わる。生ポインタを持ち続けると構造変更後に
//       ダングリングしてクラッシュするため、長く持つ参照はこちらを使う。
// ===========================================================================

#include <cassert>

#include "../Common/CCL_Types.h"

namespace CCL::ECS
{
    // 前方宣言（World の完全定義は実装側で必要）
    class World;

    /**
     * @brief コンポーネントへの安全な間接アクセス用ハンドル。
     *
     * 内部に `(World*, EntityID)` を持つだけなので軽量で、コピーも安い。
     * アクセスのたびに `World::GetComponent<T>(id)` を呼び直すので、
     *   - 構造変更後でも常に最新のアドレスを取得できる
     *   - Entity が削除された場合は nullptr / IsValid()==false になる
     *
     * @tparam T 参照したいコンポーネント型
     *
     * @code
     *   ComponentHandle<Health> h = world.GetHandle<Health>(playerId);
     *   if (h) {                  // 暗黙の bool 変換で生存確認
     *       h->hp -= 10;          // operator-> でアクセス
     *   }
     * @endcode
     */
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

        /**
         * @brief ハンドルが有効か（Entity が生きており、対象 Component を持つか）。
         * @note 実装は CCL_World.h の末尾。
         */
        bool IsValid() const;

        /**
         * @brief 明示的にポインタを取得する。
         * @return 有効なら T*、無効なら nullptr。
         * @note 実装は CCL_World.h の末尾（World::GetComponent を呼ぶため）。
         */
        T *Get() const;

        /**
         * @brief ポインタのように使えるアクセサ。
         *
         * @note 無効なハンドルへのアクセスは assert で即座に止まる。
         *       「いつのまにかダングリング」というバグを開発中に見つけやすくするため。
         */
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
