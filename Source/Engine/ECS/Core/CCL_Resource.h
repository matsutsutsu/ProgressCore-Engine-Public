#pragma once
// ===========================================================================
// 概要: World が持つシングルトン的なグローバルリソースを、型をキーに管理する仕組み
//
// 責務:
//   - リソース型 → 数値 ID の採番（ResourceIDGenerator）
//   - 任意型のリソースを1つの配列に並べるための型消去（ResourceBase / ResourceWrapper<T>）
//
// 備考: グリッドや空間分割・入力デバイス・設定オブジェクトなど、
//       「ゲーム中に一意で、Component とは別レイヤで持ちたいもの」に使う
// ===========================================================================

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace CCL::ECS
{
    // ============================================================
    // ResourceIDGenerator
    // ============================================================

    // リソースの型ごとに一意な番号を振る。番号をそのまま配列の添字に使うので、type_index の map より速く引ける
    class ResourceIDGenerator
    {
        static inline std::atomic<uint32_t> counter = 0; // 次に振る ID

    public:
        // 型 T の番号を返す。最初に呼んだときに振る。ワーカースレッドから呼んでもよい
        template <typename T>
        static uint32_t GetID()
        {
            // 関数内 static の初期化がスレッド安全なのは、同じ型どうしの話だけ
            // 別の型の初回呼び出しが別スレッドで重なると、どちらも counter を読んで同じ ID を振りうる
            // 読んで足して書く操作を fetch_add で分けられない1回にし、重複を防ぐ
            static uint32_t id = counter.fetch_add(1);
            return id;
        }
    };

    // ============================================================
    // ResourceBase / ResourceWrapper<T>
    // ============================================================

    // 型の違うリソースを1つの配列に並べるための基底。中身は ResourceWrapper<T> が持つ
    struct ResourceBase
    {
        // 仮想デストラクタを持たないと、
        // 型消去したResourceBaseのポインタをdeleteする際に派生先の
        // ResourceWrapperのデストラクタが呼ばれず、解放されずにメモリリークになる
        virtual ~ResourceBase() = default;
    };

    // 型 T のリソースを持つラッパー。添字が T の番号なので、World は static_cast で型を戻せる
    template <typename T>
    struct ResourceWrapper : public ResourceBase
    {
        T data;

        // 引数を T のコンストラクタへ転送して、その場で構築する
        template <typename... Args>
        ResourceWrapper(Args &&...args)
            : data(std::forward<Args>(args)...)
        {
        }
    };

}
