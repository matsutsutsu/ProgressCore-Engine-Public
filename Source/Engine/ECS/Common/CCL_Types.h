#pragma once
// ===========================================================================
// CCL_Types.h
// -----------
// ECSフレームワークが扱う基本的な「ID型」と「定数」を定義
//
// このヘッダーでは　ECSの基本的なIDの型を定義しているので
// 他のヘッダーから頻繁にインクルードをされるので
// 依存関係を抑えるために、シンプルな型定義と定数だけを置いている
// ===========================================================================

#include <cstdint>
#include <cstddef>

namespace CCL::ECS
{
    // ------------------------------------------------------------------
    // 汎用 ID 型エイリアス
    // ------------------------------------------------------------------
    //
    // `using 別名 = 既存の型;` で組み込み型に意味付けされた別名を与えます。
    // 単なる `uint64_t` ではなく、コード上で「これは Entity の ID
    // である」「これは Component の TypeID である」と一目で分かるようになり、
    // コードの可読性とメンテナンス性が大きく向上します。
    //
    //   例:
    //     uint64_t id;  → 何の数字か不明
    //     EntityID id;  → Entity の番号だと一目で分かる
    //
    // ------------------------------------------------------------------
    // なぜ <cstdint> の固定幅型（uint64_t / uint16_t）を使うのか
    // ------------------------------------------------------------------
    //
    // C++ の組み込み整数型が規格で保証しているのは「最低ビット数」だけで、
    // ちょうど何ビットかは処理系依存です。
    //
    //     unsigned short     : 16bit "以上"
    //     unsigned int       : 16bit "以上"
    //     unsigned long      : 32bit "以上"  ← Windows=32 / Linux,macOS=64 で実際に割れる
    //     unsigned long long : 64bit "以上"
    //
    // 対して uint64_t / uint32_t / uint16_t は「ちょうどそのビット数」を意味します。
    //
    // EntityID は「上位32bit=世代 / 下位32bit=Index」というビットパッキングを
    // 行っており、"ちょうど64bit であること" が設計上の要求です。
    // `unsigned long long`（= 64bit以上）ではその意図を表現できないため、
    // 意図がそのまま型名に出る uint64_t を使い、下の static_assert で固定します。
    // ------------------------------------------------------------------

    // 個々のエンティティを識別するID。上位32bitに世代、下位32bitにIndex。
    using EntityID = uint64_t;

    // コンポーネント型（クラス）を識別するID（Position(1), Velocity(2)など）
    using TypeID = uint16_t;

    // 個々のシステムを識別するID
    using SystemID = uint16_t;

    // ------------------------------------------------------------------
    // サイズ前提のコンパイル時検証
    // ------------------------------------------------------------------
    //
    // 将来ビットパッキングの前提を知らない人が型を差し替えた場合に、
    // 実行時の無言の破損ではなくコンパイルエラーで気付けるようにしています。
    // ------------------------------------------------------------------

    static_assert(sizeof(EntityID) == 8,
                  "EntityID は 上位32bit=世代 / 下位32bit=Index のビットパッキングを前提としているため、"
                  "ちょうど64bitである必要があります");

    static_assert(sizeof(TypeID) == 2,
                  "TypeID は 16bit（最大65,535種類のコンポーネント型）を前提としています");

    static_assert(sizeof(SystemID) == 2,
                  "SystemID は 16bit を前提としています");

    // ------------------------------------------------------------------
    // 無効値 (constexpr)
    // ------------------------------------------------------------------
    //
    // unsigned 型を -1 にキャストすると、その型の最大値（全ビット1）になります。
    // ECS では「オブジェクトの実体は消さずに、IDを無効化して論理削除」する
    // パターンが頻出するため、各IDに対する「無効値」を定数として用意しています。
    //
    // constexpr を使うことで、コンパイル時定数として扱われ、ヘッダで定義しても
    // 多重定義エラーになりません（マクロより型安全）。
    // ------------------------------------------------------------------

    // 無効な EntityID（全ビット1）
    constexpr EntityID InvalidEntityID = static_cast<EntityID>(-1);

    // 無効な TypeID（全ビット1）
    constexpr TypeID InvalidTypeID = static_cast<TypeID>(-1);

    // 無効な SystemID（全ビット1）
    constexpr SystemID InvalidSystemID = static_cast<SystemID>(-1);

    // 配列・チャンク内インデックスの無効値（size_t 用）
    constexpr size_t InvalidIndex = static_cast<size_t>(-1);

    // ------------------------------------------------------------------
    // EntityHandle
    // ------------------------------------------------------------------

    /**
     * @brief EntityID を Index と Generation に分解・合成するユーティリティ。
     *
     * EntityID は 64bit 整数で、上位32bitに「世代カウンタ」、下位32bitに
     * 「配列インデックス」をパックして格納します。これにより、削除済みの古い
     * EntityID が再利用された別のEntityを誤って指してしまう「ダングリングID」
     * 問題を世代チェックで防ぐことができます。
     *
     * @code
     *   EntityID id = EntityHandle::Combine(index, generation);
     *   uint32_t i  = EntityHandle::GetIndex(id);
     *   uint32_t g  = EntityHandle::GetGeneration(id);
     * @endcode
     */
    struct EntityHandle
    {
        // 下位32bit抽出用マスク
        //
        // 0xFFFFFFFFULLは下位32bitが1で上位32bitが0の64bit定数
        // 論理積（AND）を取ることで、EntityIDの下位32bit（配列インデックス）だけを取り出せる
        static constexpr EntityID INDEX_MASK = 0xFFFFFFFFULL;

        // 世代部のシフト量（32bit分上にずらす）
        static constexpr EntityID GEN_SHIFT = 32;

        // EntityID から配列インデックス部を取り出す
        static uint32_t GetIndex(EntityID id)
        {
            // 64bitのEntityIDをINDEX_MASKとビットANDすることで、
            // 下位32bitのインデックスだけが残る
            // & は両方のビットが1の場合だけ１を返す
            return static_cast<uint32_t>(id & INDEX_MASK);
        }

        // EntityID から世代カウンタ部を取り出す
        static uint32_t GetGeneration(EntityID id)
        {
            // 32bit右シフトすることで、上位32bitの世代カウンタだけが残る
            // 例えば、EntityIDが0x00000002 / 00000003の場合、右シフトすると0x00000002になり、これが世代カウンタとなる
            return static_cast<uint32_t>(id >> GEN_SHIFT);
        }

        // Index と Generation から EntityID を合成する
        static EntityID Combine(uint32_t index, uint32_t gen)
        {
            // 世代のbitを左に32bitずらして上位32bitの位置にセットする
            // | はビット単位の論理和演算子で、これにより世代とインデックスが1つの64bit整数にパックされる
            return (static_cast<EntityID>(gen) << GEN_SHIFT) | static_cast<EntityID>(index);
        }
    };

} // namespace CCL::ECS
