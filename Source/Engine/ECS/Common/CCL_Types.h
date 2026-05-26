#pragma once
// ===========================================================================
// CCL_Types.h
// -----------
// ECSフレームワークが扱う基本的な「ID型」と「定数」を定義します。
//
// このヘッダは ECS の最深部・最頻出のヘッダであり、極力依存を持たせない方針
// です（<cstdint> のみ）。他の CCL ヘッダはこれをインクルードしますが、
// 逆はありません。
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
    // 単なる `unsigned long long` ではなく、コード上で「これは Entity の ID
    // である」「これは Component の TypeID である」と一目で分かるようになり、
    // コードの可読性とメンテナンス性が大きく向上します。
    //
    //   例:
    //     unsigned long long id;  → 何の数字か不明
    //     EntityID           id;  → Entity の番号だと一目で分かる
    //
    // ------------------------------------------------------------------

    /// @brief 個々のエンティティを識別するID。上位32bitに世代、下位32bitにIndex。
    using EntityID    = unsigned long long;

    /// @brief コンポーネント型（クラス）を識別するID（Position(1), Velocity(2)など）
    using TypeID      = unsigned short;

    /// @brief コンポーネントの「実体」を識別するID（型ではなくインスタンス用）
    using ComponentID = unsigned long long;

    /// @brief 個々のシステムを識別するID
    using SystemID    = unsigned short;


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

    /// @brief 無効な EntityID（全ビット1）
    constexpr EntityID    InvalidEntityID    = static_cast<EntityID>(-1);

    /// @brief 無効な TypeID（全ビット1）
    constexpr TypeID      InvalidTypeID      = static_cast<TypeID>(-1);

    /// @brief 無効な ComponentID（全ビット1）
    constexpr ComponentID InvalidComponentID = static_cast<ComponentID>(-1);

    /// @brief 無効な SystemID（全ビット1）
    constexpr SystemID    InvalidSystemID    = static_cast<SystemID>(-1);

    /// @brief 配列・チャンク内インデックスの無効値（size_t 用）
    constexpr size_t      InvalidIndex       = static_cast<size_t>(-1);


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
        /// @brief 下位32bit抽出用マスク
        static constexpr EntityID INDEX_MASK = 0xFFFFFFFFULL;

        /// @brief 世代部のシフト量（32bit分上にずらす）
        static constexpr EntityID GEN_SHIFT  = 32;

        /// @brief EntityID から配列インデックス部を取り出す
        static uint32_t GetIndex(EntityID id)
        {
            return static_cast<uint32_t>(id & INDEX_MASK);
        }

        /// @brief EntityID から世代カウンタ部を取り出す
        static uint32_t GetGeneration(EntityID id)
        {
            return static_cast<uint32_t>(id >> GEN_SHIFT);
        }

        /// @brief Index と Generation から EntityID を合成する
        static EntityID Combine(uint32_t index, uint32_t gen)
        {
            return (static_cast<EntityID>(gen) << GEN_SHIFT)
                 |  static_cast<EntityID>(index);
        }
    };

} // namespace CCL::ECS
