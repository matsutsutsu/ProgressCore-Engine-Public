// =====================================================================
// EntityLink.h
//   シリアライズ可能な「エンティティへの参照」を表す軽量型。
//
//   修正履歴:
//     v2: 以下の問題を解消
//       [C2593] operator==(const EntityLink&) と operator EntityID() が共存すると、
//               EntityLink == EntityLink でコンパイラが
//               「メンバーの== を使う」か「両辺を EntityID に変換して== を使う」か
//               判断できず、あいまいな変換エラーになる。
//               → メンバーの operator== / operator!= を削除。
//                 暗黙変換 operator EntityID() に一本化する。
//
//       [Sol2]  using CCL::ECS::EntityID = unsigned long long; を再宣言すると
//               Sol2 の型トレイト (unqualified_t, is_transparent_argument 等) が
//               誤った型情報でインスタンス化を試みてエラーになる。
//               → 再宣言を削除し、CCL_Common.h を include する方式に変更。
//
//   設計上の比較まとめ:
//     link1 == link2      → 両辺が EntityID に変換 → ull == ull  (一意・高速)
//     link1 == 0          → link1 が EntityID に変換 → ull == 0   (OK)
//     link1 == someID     → link1 が EntityID に変換 → ull == ull  (OK)
//     EntityID x = link1  → 暗黙変換で取れる (既存コードと互換)
// =====================================================================
#pragma once
#include "Engine/ECS/Common/CCL_Types.h"   // CCL::ECS::EntityID, InvalidEntityID の正式な定義元

namespace CCL::Serialization {

    struct EntityLink {
        CCL::ECS::EntityID id = CCL::ECS::InvalidEntityID;

        // ---- 構築 ----
        constexpr EntityLink() = default;
        constexpr EntityLink(CCL::ECS::EntityID e) : id(e) {}

        // ---- 暗黙変換 (EntityID として扱える) ----
        //
        // operator== / operator!= は意図的に持たない。
        //   代わりに、この暗黙変換で EntityID 同士の比較に帰着させる。
        //
        //   なぜそうするか:
        //   メンバー operator== と operator EntityID() が両方あると、
        //   link1 == link2 の時に
        //     パス A: link1.operator==(link2)   を使う
        //     パス B: (EntityID)link1 == (EntityID)link2 を使う
        //   が等コストで存在し、C2593 「あいまいな変換」になる。
        //   メンバー == を消せば、常にパス B のみ → 一意に解決。
        constexpr operator CCL::ECS::EntityID() const { return id; }

        // ---- 代入 ----
        EntityLink& operator=(CCL::ECS::EntityID e) { id = e; return *this; }

        // ---- 状態確認 ----
        bool IsValid() const {
            return id != CCL::ECS::InvalidEntityID && id != 0;
        }

        void Reset() {
            id = CCL::ECS::InvalidEntityID;
        }
    };

    // sizeof 保証: offsetof で安全に扱えるよう、EntityID と同サイズであること
    static_assert(sizeof(EntityLink) == sizeof(CCL::ECS::EntityID),
        "EntityLink must be same size as EntityID.");

} // namespace CCL::Serialization