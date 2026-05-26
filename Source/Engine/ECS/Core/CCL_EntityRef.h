#pragma once
// ===========================================================================
// CCL_EntityRef.h
// ---------------
// エンティティ生成直後の「メソッドチェーン的なセットアップ」を可能にする
// 軽量ラッパー EntityRef を提供します。
//
// EntityRef 自体は (World*, EntityID) の2フィールドだけの構造体で、
// 寿命管理は行いません。完全に「使い捨ての操作チケット」として使います。
//
// @note
//   メンバ関数 Set / Patch の実装は World::AddComponent などのテンプレートを
//   呼び出すため、CCL_World.h の完全定義が必要です。そのため宣言だけここに
//   置き、定義は CCL_World.h の末尾（World 定義後）にインライン実装しています。
// ===========================================================================

#include "../Common/CCL_Types.h"


namespace CCL::ECS::Core
{
    // 前方宣言（World の定義は不要）
    class World;


    /**
     * @brief エンティティの初期セットアップを宣言的に書くための軽量ラッパー。
     *
     * `World::Spawn()` の戻り値として返され、メソッドチェーンで一気に
     * コンポーネントを設定できます。
     *
     * @code
     *   auto e = world.Spawn(archetype)
     *                 .Set(Position{0, 0})
     *                 .Set(Velocity{1, 0})
     *                 .Patch<Transform>([](auto& t){ t.scale = {2, 2}; });
     * @endcode
     *
     * @note
     *   - World ポインタは弱参照（解放責任を持たない）
     *   - 一時オブジェクトとして使い捨てる前提のため、コピー・ムーブは
     *     C++ のデフォルト挙動に任せている
     *   - 暗黙の EntityID 変換演算子を持ち、変数代入で EntityID を保存可能
     */
    struct EntityRef
    {
        World*   world;
        EntityID id;

        /**
         * @brief コンポーネントの値を設定する（メソッドチェーン用）。
         * @param component 完全転送される値（左辺値/右辺値どちらも可）
         * @return *this を返し、`.Set(...).Set(...)` のチェーンを可能にする
         *
         * @note 実装は CCL_World.h の末尾。World::AddComponent を呼び出す。
         */
        template <typename T>
        EntityRef& Set(T&& component);

        /**
         * @brief 既存コンポーネントの中身を、ラムダ式で部分書き換えする。
         *
         * Prefab 生成直後など「特定のフィールドだけ差し替えたい」場面で有用。
         *
         * @code
         *   world.Spawn(playerArch)
         *        .Patch<Transform>([&](auto& t) { t.position = startPos; });
         * @endcode
         *
         * @note 実装は CCL_World.h の末尾。World::PatchComponent を呼び出す。
         */
        template <typename T, typename Func>
        EntityRef& Patch(Func&& func);


        // ------------------------------------------------------------
        // EntityID への暗黙変換 / 明示取得
        // ------------------------------------------------------------

        /// @brief `EntityID e = world.Spawn(...);` のように暗黙変換を許す
        operator EntityID() const { return id; }

        /// @brief 明示的に EntityID を取り出す
        EntityID ID() const { return id; }
    };

} // namespace CCL::ECS::Core
