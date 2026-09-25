#pragma once
// ===========================================================================
// 概要: World が持つシングルトン的なグローバルリソースを、型をキーに管理する仕組み。
//
// 責務:
//   - リソース型 → 数値 ID の採番（ResourceIDGenerator）
//   - 任意型のリソースを1つの配列に並べるための型消去（ResourceBase / ResourceWrapper<T>）
//
// 備考: グリッドや空間分割・入力デバイス・設定オブジェクトなど、
//       「ゲーム中に一意で、Component とは別レイヤで持ちたいもの」に使う。
// ===========================================================================

#include <cstdint>
#include <memory>
#include <utility> // std::forward

namespace CCL::ECS
{
    // ============================================================
    // ResourceIDGenerator
    // ============================================================

    /**
     * @brief リソースの「型 → 数値ID」変換を高速に行うジェネレータ。
     *
     * 関数テンプレート内のローカル static で、型 T ごとに一意な ID を付与する。
     * ID をそのまま配列の添字に使えるので、std::type_index をキーにした
     * unordered_map よりも検索が速い。
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

    // ============================================================
    // ResourceBase / ResourceWrapper<T>
    // ============================================================

    /**
     * @brief 任意型のリソースを単一の配列で管理するための「型消去」基底クラス。
     *
     * 実際のデータ T は派生クラス ResourceWrapper<T> が持つ。基底を1つにすることで、
     * World は `std::vector<std::unique_ptr<ResourceBase>>` という形で
     * 「型をまたいだリソース一覧」を持てる。
     */
    struct ResourceBase
    {
        virtual ~ResourceBase() = default;
    };

    /**
     * @brief 任意型 T のリソースを格納する型安全なラッパー。
     *
     * World は `ResourceBase*` として保管したポインタを `ResourceWrapper<T>*` に
     * static_cast して型を復元する。添字が型 T の ID なので、型の食い違いは起きない。
     *
     * @tparam T 保持するリソース型
     */
    template <typename T>
    struct ResourceWrapper : public ResourceBase
    {
        T data;

        /**
         * @brief T のコンストラクタ引数を完全転送で受け取り、その場で構築する。
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

} // namespace CCL::ECS
