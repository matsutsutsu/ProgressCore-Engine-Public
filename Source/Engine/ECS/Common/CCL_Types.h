#pragma once
// ===========================================================================
// 概要: ECS が扱う基本の ID 型と無効値の定義。
//
// 責務:
//   - EntityID / TypeID / SystemID の型エイリアスとサイズ前提の検証
//   - 各 ID の無効値
//   - EntityID の Index / Generation の分解・合成（EntityHandle）
//
// 備考: 多くのヘッダから include されるので、依存を増やさないよう
//       単純な型定義と定数だけを置く。
// ===========================================================================

#include <cstdint>
#include <cstddef>

namespace CCL::ECS
{
    // ============================================================
    // ID 型エイリアス
    // ============================================================
    //
    // 単なる uint64_t ではなく「Entity の ID である」と型名で分かるよう別名を付ける。
    //   uint64_t id;  → 何の数字か不明
    //   EntityID id;  → Entity の番号だと一目で分かる
    //
    // なぜ <cstdint> の固定幅型（uint64_t / uint16_t）を使うのか:
    //   C++ の組み込み整数型が規格で保証しているのは「最低ビット数」だけで、
    //   ちょうど何ビットかは処理系依存。
    //
    //     unsigned short     : 16bit "以上"
    //     unsigned int       : 16bit "以上"
    //     unsigned long      : 32bit "以上"  ← Windows=32 / Linux,macOS=64 で実際に割れる
    //     unsigned long long : 64bit "以上"
    //
    //   対して uint64_t / uint32_t / uint16_t は「ちょうどそのビット数」を意味する。
    //   EntityID は「上位32bit=世代 / 下位32bit=Index」のビットパッキングをしており、
    //   "ちょうど64bit であること" が設計上の要求。unsigned long long（64bit以上）では
    //   その意図を表現できないので uint64_t を使い、下の static_assert で固定する。

    using EntityID = uint64_t; // 個々の Entity の ID。上位32bitに世代、下位32bitに Index
    using TypeID   = uint16_t; // コンポーネント型（クラス）の ID（Position=1, Velocity=2 など）
    using SystemID = uint16_t; // 個々の System の ID

    // ビットパッキングの前提を知らずに型を差し替えたとき、
    // 実行時の無言の破損ではなくコンパイルエラーで気付けるようにする

    static_assert(sizeof(EntityID) == 8,
                  "EntityID は 上位32bit=世代 / 下位32bit=Index のビットパッキングを前提としているため、"
                  "ちょうど64bitである必要があります");

    static_assert(sizeof(TypeID) == 2,
                  "TypeID は 16bit（最大65,535種類のコンポーネント型）を前提としています");

    static_assert(sizeof(SystemID) == 2,
                  "SystemID は 16bit を前提としています");

    // ============================================================
    // 無効値
    // ============================================================
    //
    // unsigned 型を -1 にキャストすると、その型の最大値（全ビット1）になる。
    // ECS では「実体は消さずに ID を無効化して論理削除」するパターンが多いので、
    // 各 ID の無効値を定数で用意する。
    // constexpr なのでヘッダで定義しても多重定義にならない（マクロより型安全）。

    constexpr EntityID InvalidEntityID = static_cast<EntityID>(-1);
    constexpr TypeID   InvalidTypeID   = static_cast<TypeID>(-1);
    constexpr SystemID InvalidSystemID = static_cast<SystemID>(-1);
    constexpr size_t   InvalidIndex    = static_cast<size_t>(-1); // 配列・Chunk 内インデックス用

    // ============================================================
    // EntityHandle
    // ============================================================

    /**
     * @brief EntityID を Index と Generation に分解・合成するユーティリティ。
     *
     * EntityID は 64bit 整数で、上位32bitに「世代カウンタ」、下位32bitに
     * 「配列インデックス」をパックして格納する。削除済みの古い EntityID が、
     * 同じ Index を再利用した別の Entity を誤って指す「ダングリングID」問題を
     * 世代チェックで防ぐため。
     *
     * @code
     *   EntityID id = EntityHandle::Combine(index, generation);
     *   uint32_t i  = EntityHandle::GetIndex(id);
     *   uint32_t g  = EntityHandle::GetGeneration(id);
     * @endcode
     */
    struct EntityHandle
    {
        // 下位32bit抽出用マスク。
        // 0xFFFFFFFFULL は下位32bitが1、上位32bitが0の64bit定数。
        // 論理積（AND）を取ると、EntityID の下位32bit（配列インデックス）だけを取り出せる
        static constexpr EntityID INDEX_MASK = 0xFFFFFFFFULL;

        // 世代部のシフト量（32bit分上にずらす）
        static constexpr EntityID GEN_SHIFT = 32;

        // EntityID から配列インデックス部を取り出す
        static uint32_t GetIndex(EntityID id)
        {
            // INDEX_MASK とビット AND を取ると、下位32bitのインデックスだけが残る
            // （& は両方のビットが1の場合だけ1を返す）
            return static_cast<uint32_t>(id & INDEX_MASK);
        }

        // EntityID から世代カウンタ部を取り出す
        static uint32_t GetGeneration(EntityID id)
        {
            // 32bit 右シフトすると、上位32bitの世代カウンタだけが残る
            // 例: EntityID が 0x00000002'00000003 なら、右シフトで 0x00000002（＝世代）になる
            return static_cast<uint32_t>(id >> GEN_SHIFT);
        }

        // Index と Generation から EntityID を合成する
        static EntityID Combine(uint32_t index, uint32_t gen)
        {
            // 世代を左に32bitずらして上位32bitに置き、
            // ビット単位の論理和（|）でインデックスと1つの64bit整数にパックする
            return (static_cast<EntityID>(gen) << GEN_SHIFT) | static_cast<EntityID>(index);
        }
    };

} // namespace CCL::ECS
