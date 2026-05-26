#pragma once
// ===========================================================================
// CCL_Archetype.h
// ---------------
// 「あるエンティティが持つコンポーネント型の組み合わせ（構成）」を表現する
// Archetype と、その操作ヘルパー ArchetypeHelper を提供します。
//
// Archetype は ECS の論理構造の中核で、Chunk が「どんなデータを格納するか」
// を決定する設計図にあたります。
// ===========================================================================

#include <vector>
#include <algorithm>

#include "CCL_Types.h"
#include "CCL_TypeInfo.h"


namespace CCL::ECS
{
    // ------------------------------------------------------------------
    // Archetype
    // ------------------------------------------------------------------

    /**
     * @brief コンポーネント型の組み合わせ（構成）を表すクラス。
     *
     * Archetype はあくまで「型の集合」であり、実体データは保持しません。
     * 内部では `std::vector<TypeData>` を TypeID 昇順でソート保持しているため、
     *   - 一致判定が O(N) で高速
     *   - 包含判定が O(N+M) で可能
     *
     * というメリットがあります（同じ構成なら順序が違ってもソート後に一致）。
     *
     * @code
     *   Archetype a;
     *   a.Add<Position>();
     *   a.Add<Velocity>();
     *   // a は [Position(1), Velocity(2)] で常にソート済み
     * @endcode
     */
    class Archetype
    {
    public:
        Archetype()  = default;
        ~Archetype() = default;

        // コピー・代入は内包する std::vector<TypeData> がきちんと処理する
        Archetype(const Archetype& other)            = default;
        Archetype& operator=(const Archetype& other) = default;


        /**
         * @brief 型 T を Archetype に追加する（TypeID 昇順ソート維持）。
         * @return 追加先（または既存）のインデックス
         */
        template <class T>
        size_t Add()
        {
            TypeData type;
            type.Create<T>();
            return Add(type);
        }

        /**
         * @brief 任意の TypeData を Archetype に追加する。
         *
         * 内部では `std::lower_bound` で O(log N) の二分探索により挿入位置を
         * 決定し、std::vector に挿入します。重複が見つかった場合は何もせず
         * その位置を返します。
         *
         * @return 追加先（または既存）のインデックス
         */
        size_t Add(const TypeData& typeData)
        {
            // 挿入位置を二分探索で見つける
            auto it = std::lower_bound(
                _data.begin(),
                _data.end(),
                typeData.id,
                [](const TypeData& td, TypeID targetId) {
                    return td.id < targetId;
                }
            );

            // 既に同じ TypeID があれば追加せず、その位置を返す
            if (it != _data.end() && it->id == typeData.id) {
                return std::distance(_data.begin(), it);
            }

            const size_t index = std::distance(_data.begin(), it);
            _data.insert(it, typeData);
            return index;
        }

        /// @brief 型 T を Archetype から削除する
        template <class T>
        void Sub()
        {
            Sub(TypeInfo<T>::ID());
        }

        /**
         * @brief TypeID を直接指定して Archetype から該当要素を削除する。
         *
         * Component が外されたときの Archetype 遷移で使用されます。
         * こちらも O(log N) の二分探索後に erase します。
         */
        void Sub(TypeID id)
        {
            auto it = std::lower_bound(
                _data.begin(),
                _data.end(),
                id,
                [](const TypeData& td, TypeID targetId) {
                    return td.id < targetId;
                }
            );

            if (it != _data.end() && it->id == id) {
                _data.erase(it);
            }
        }


        // ------------------------------------------------------------
        // 検索・比較
        // ------------------------------------------------------------

        /**
         * @brief TypeID から、その TypeData が格納されているインデックスを返す。
         * @return 見つかればインデックス、見つからなければ InvalidIndex
         * @note O(log N)
         */
        size_t GetComponentIndex(TypeID id) const
        {
            auto it = std::lower_bound(
                _data.begin(),
                _data.end(),
                id,
                [](const TypeData& td, TypeID targetId) {
                    return td.id < targetId;
                }
            );

            if (it != _data.end() && it->id == id) {
                return std::distance(_data.begin(), it);
            }
            return InvalidIndex;
        }

        /**
         * @brief 二つの Archetype が「同じコンポーネント構成」かを判定する。
         * @note TypeData が常にソート済みのため O(N) で高速比較できる。
         */
        bool operator==(const Archetype& r) const
        {
            if (_data.size() != r._data.size()) return false;
            return std::equal(_data.begin(), _data.end(), r._data.begin());
        }


        // ------------------------------------------------------------
        // 読み取り専用アクセサ
        // ------------------------------------------------------------
        const TypeData& at(size_t i)         const { return _data.at(i); }
        const TypeData& operator[](size_t i) const { return _data[i]; }

        size_t size()   const { return _data.size(); }
        bool   empty()  const { return _data.empty(); }

        // 範囲for対応
        std::vector<TypeData>::const_iterator begin() const { return _data.cbegin(); }
        std::vector<TypeData>::const_iterator end()   const { return _data.cend(); }

    private:
        // 継承ではなく包含。常に TypeID 昇順ソート済みであることが不変条件。
        std::vector<TypeData> _data;
    };


    // ------------------------------------------------------------------
    // ArchetypeHelper
    // ------------------------------------------------------------------

    /**
     * @brief Archetype を扱うためのユーティリティ関数群。
     *
     * よく使う「型パラメータパックから Archetype を一発生成」「包含判定」
     * といった処理を切り出しています。
     */
    class ArchetypeHelper
    {
    public:
        /**
         * @brief 任意個数のコンポーネント型から Archetype を自動生成する。
         *
         * @code
         *   auto a = ArchetypeHelper::Generate<Position, Velocity, Render>();
         *   // → [Position(1), Velocity(2), Render(5)]
         * @endcode
         *
         * 内部で `(a.Add<Types>(), ...);` という C++17 fold expression を
         * 使い、Add を引数の数だけ展開しています。
         */
        template <class ...Types>
        static Archetype Generate()
        {
            Archetype a;
            (a.Add<Types>(), ...);
            return a;
        }

        /**
         * @brief Archetype l が Archetype r を「全て包含している」かを判定する。
         *
         * SystemMatcher（システムフィルタの適合判定）に使用します。
         *   - 例: l = [Position, Velocity, Render], r = [Position, Velocity] → true
         *   - 例: l = [Position, Velocity], r = [Position, Velocity, Render] → false
         *
         * 両者ソート済みなので `std::includes` で O(N+M) 判定が可能です。
         *
         * @param l  チャンク側 Archetype（包含する側）
         * @param r  フィルタ側 Archetype（包含される側）
         * @return r が l に包含されていれば true
         */
        static bool LhasR(const Archetype& l, const Archetype& r)
        {
            // フィルタ r が空なら、無条件で全チャンクが対象
            if (r.size() == 0) return true;
            // チャンク l の方が少ない要素しか持たないなら絶対に包含不可
            if (l.size() < r.size()) return false;

            return std::includes(
                l.begin(), l.end(),
                r.begin(), r.end(),
                [](const TypeData& a, const TypeData& b) {
                    return a.id < b.id;
                });
        }
    };

    // ------------------------------------------------------------------
    // 補足: STL を使わない LhasR 実装案（参考・将来差し替え用）
    // ------------------------------------------------------------------
    // 2本のインデックス（指）で同時走査する O(N+M) 実装。
    // std::includes よりも分岐予測に乗りやすく、若干高速になるケースがあります。
    //
    //  size_t i = 0, j = 0;
    //  while (i < l.size() && j < r.size()) {
    //      if      (l[i].id == r[j].id) { ++i; ++j; }
    //      else if (l[i].id <  r[j].id) { ++i; }
    //      else                          { return false; }
    //  }
    //  return j == r.size();
    // ------------------------------------------------------------------

} // namespace CCL::ECS
