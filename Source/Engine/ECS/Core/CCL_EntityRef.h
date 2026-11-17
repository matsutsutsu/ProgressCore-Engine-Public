#pragma once
// ===========================================================================
// 概要: Entity 生成直後のセットアップをメソッドチェーンで書くための軽量ラッパー EntityRef
//
// 責務:
//   - Set / Patch によるコンポーネントの初期設定をチェーンで書けるようにする
//   - EntityID への暗黙変換
//
// 備考: (World*, EntityID) だけの使い捨て構造体で、寿命管理はしない
//       Set / Patch は World のテンプレートを呼ぶので World の完全定義が要る
//       そのため宣言だけここに置き、定義は CCL_World.h の末尾に書いている
// ===========================================================================

#include "../Common/CCL_Types.h"

namespace CCL::ECS
{
    // 宣言だけなら World の定義は不要
    class World;

    // World::Spawn が返す、生成した Entity にコンポーネントを続けて設定するためのラッパー。World は所有しない
    // 例: EntityID e = world.Spawn(arch).Set(Position{0, 0}).Patch<Transform>([](auto &t) { t.scale = {2, 2}; });
    struct EntityRef
    {
        World   *world; // 所有しない
        EntityID id;

        // コンポーネントを追加する予約を積み、*this を返す（実装は CCL_World.h の末尾）
        template <typename T>
        EntityRef &Set(T &&component);

        // 既存のコンポーネントをラムダで部分的に書き換える予約を積み、*this を返す（実装は CCL_World.h の末尾）
        template <typename T, typename Func>
        EntityRef &Patch(Func &&func);

        // `EntityID e = world.Spawn(...);` のように暗黙変換を許す
        operator EntityID() const { return id; }

        EntityID ID() const { return id; }
    };

}
