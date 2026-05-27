#pragma once
// ===========================================================================
// CCL_Resource.h
// --------------
// World が保持する「シングルトン的なグローバルリソース」を、型安全かつ
// 型消去で管理するための仕組みを提供します。
//
// 例:
//   - グリッドや空間分割
//   - 入力デバイス
//   - 設定オブジェクト
//
// など、「ゲーム中に一意であり、ECS の Component とは別レイヤで持ちたい
// オブジェクト」を、Component と同様に型キー (TypeID 風) でアクセスできるよう
// にします。
// ===========================================================================

#include <cstdint>
#include <memory>
#include <utility>  // std::forward


namespace CCL::ECS::Core
{
    // ------------------------------------------------------------------
    // ResourceIDGenerator
    // ------------------------------------------------------------------

    /**
     * @brief リソースの「型 → 数値ID」変換を高速に行うジェネレータ。
     *
     * TypeInfo<T> と同じ「inline static + ローカル static」テクニックを使い、
     * 型 T ごとに一意な ID を付与します。これにより、unordered_map のキーに
     * std::type_index を使うより数倍高速にリソース検索ができます。
     *
     * @code
     *   uint32_t gridId  = ResourceIDGenerator::GetID<GridResource>();
     *   uint32_t inputId = ResourceIDGenerator::GetID<InputResource>();
     *   // 異なる型 → 異なる ID
     * @endcode
     */
    class ResourceIDGenerator
    {
        static inline uint32_t counter = 0;

    public:
        /**
         * @brief 型 T に対する一意な ID を返す。
         * @note  最初に呼ばれた時に採番され、以降は同じ値が返る。
         */
        template <typename T>
        static uint32_t GetID()
        {
            static uint32_t id = counter++;
            return id;
        }
    };


    // ------------------------------------------------------------------
    // ResourceBase / ResourceWrapper<T>
    // ------------------------------------------------------------------

    /**
     * @brief 任意型のリソースを単一の配列で管理するための「型消去」基底クラス。
     *
     * 派生クラス ResourceWrapper<T> が実際にデータ T を保持しますが、World は
     * `std::vector<std::unique_ptr<ResourceBase>>` という形で「型をまたいだ
     * リソース一覧」を持てるようになります。
     */
    struct ResourceBase
    {
        virtual ~ResourceBase() = default;
    };

    /**
     * @brief 任意型 T のリソースを格納する型安全なラッパー。
     *
     * `ResourceBase*` として保管されているポインタを `ResourceWrapper<T>*` に
     * 動的キャストすることで、安全に型を復元してデータにアクセスできます。
     *
     * @tparam T 保持するリソース型
     */
    template <typename T>
    struct ResourceWrapper : public ResourceBase
    {
        T data;

        /**
         * @brief T のコンストラクタ引数を完全転送で受け取り、その場で構築。
         *
         * @code
         *   ResourceWrapper<GridResource>(100, 100); // 100x100 のグリッドを構築
         * @endcode
         */
        template <typename... Args>
        ResourceWrapper(Args &&...args)
            : data(std::forward<Args>(args)...)
        {
        }
    };

} // namespace CCL::ECS::Core
