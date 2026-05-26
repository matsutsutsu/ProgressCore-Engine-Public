#pragma once
// ===========================================================================
// CCL_ComponentHandle.h
// ---------------------
// 生ポインタ T* の代わりに保持できる「安全なコンポーネントアクセス用チケット」
// ComponentHandle<T> を提供します。
//
// Chunk 内のコンポーネントは、エンティティの追加・削除や Archetype 遷移によって
// メモリ上の位置が変動します。生ポインタを保持していると、構造変更後に
// ダングリングしてクラッシュする危険がありますが、ComponentHandle はアクセス
// するたびに World 経由で最新のアドレスを引き直すため安全です。
// ===========================================================================

#include <cassert>

#include "../Common/CCL_Types.h"


namespace CCL::ECS::Core
{
    // 前方宣言（World の完全定義は実装側で必要）
    class World;


    /**
     * @brief コンポーネントへの安全な間接アクセス用ハンドル。
     *
     * 内部に `(World*, EntityID)` を保持するだけのため軽量で、コピーも安価です。
     * アクセスのたびに `World::GetComponent<T>(id)` を呼び直すため、
     *   - 構造変更後でも常に最新のアドレスを取得できる
     *   - エンティティが削除された場合は nullptr / IsValid()==false になる
     *
     * という安全性が得られます。
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
        /// @brief 無効ハンドルを構築する
        ComponentHandle()
            : _world(nullptr)
            , _id(InvalidEntityID)
        {
        }

        /// @brief 有効ハンドルを構築する（通常は World::GetHandle 経由で生成）
        ComponentHandle(World* world, EntityID id)
            : _world(world)
            , _id(id)
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
        T* Get() const;

        /**
         * @brief ポインタのように使えるアクセサ。
         *
         * @note 無効なハンドルへのアクセスは assert で即座に検出されます。
         *       これにより「いつのまにかダングリング」というバグを開発中に
         *       発見しやすくなります。
         */
        T* operator->() const
        {
            T* ptr = Get();
            assert(ptr && "Accessing invalid or destroyed component! (Handle expired)");
            return ptr;
        }

        /// @brief `if (handle) { ... }` と書けるようにする
        explicit operator bool() const { return IsValid(); }

        /// @brief 同じ World 上の同じ Entity を指すかで一致判定
        bool operator==(const ComponentHandle<T>& other) const
        {
            return _world == other._world && _id == other._id;
        }

    private:
        World*   _world;
        EntityID _id;
    };

} // namespace CCL::ECS::Core
