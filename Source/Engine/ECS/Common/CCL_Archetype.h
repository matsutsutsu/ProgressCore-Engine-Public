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
                m_data.begin(),
                m_data.end(),
                typeData.id,
                [](const TypeData& td, TypeID targetId) {
                    return td.id < targetId;
                }
            );

            // 既に同じ TypeID があれば追加せず、その位置を返す
            // これよくわからん
            if (it != m_data.end() && it->id == typeData.id) {
                return std::distance(m_data.begin(), it);
            }

            const size_t index = std::distance(m_data.begin(), it);
            m_data.insert(it, typeData);
            return index;
        }

        // 型 T を Archetype から削除する
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
                m_data.begin(),
                m_data.end(),
                id,
                [](const TypeData& td, TypeID targetId) {
                    return td.id < targetId;
                }
            );

            if (it != m_data.end() && it->id == id) {
                m_data.erase(it);
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
                m_data.begin(),
                m_data.end(),
                id,
                [](const TypeData& td, TypeID targetId) {
                    return td.id < targetId;
                }
            );

            if (it != m_data.end() && it->id == id) {
                return std::distance(m_data.begin(), it);
            }
            return InvalidIndex;
        }

        /**
         * @brief 二つの Archetype が「同じコンポーネント構成」かを判定する。
         * @note TypeData が常にソート済みのため O(N) で高速比較できる。
         */
        bool operator==(const Archetype& r) const
        {
            if (m_data.size() != r.m_data.size()) return false;
            return std::equal(m_data.begin(), m_data.end(), r.m_data.begin());
        }


        // ------------------------------------------------------------
        // 読み取り専用アクセサ
        // ------------------------------------------------------------
        const TypeData& at(size_t i)         const { return m_data.at(i); }
        const TypeData& operator[](size_t i) const { return m_data[i]; }

        size_t size()   const { return m_data.size(); }
        bool   empty()  const { return m_data.empty(); }

        // 範囲for対応
        std::vector<TypeData>::const_iterator begin() const { return m_data.cbegin(); }
        std::vector<TypeData>::const_iterator end()   const { return m_data.cend(); }

    private:
        // 継承ではなく包含。常に TypeID 昇順ソート済みであることが不変条件。
        std::vector<TypeData> m_data;
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
			// ... のTypesを順に展開して a.Add<Types>() を呼び出す fold expression
            (a.Add<Types>(), ...);
            return a;
        }

        /**
         * @brief Archetype chunk_L が Archetype filter_R を「全て包含している」かを判定する。
         *
         * SystemMatcher（システムフィルタの適合判定）に使用します。
         *   - 例: chunk_L = [Position, Velocity, Render], filter_R = [Position, Velocity] → true
         *   - 例: chunk_L = [Position, Velocity], filter_R = [Position, Velocity, Render] → false
         *
         * 両者ソート済みなので `std::includes` で O(N+M) 判定が可能です。
         *
         * @param chunk_L  チャンク側 Archetype（包含する側）
         * @param filter_R  フィルタ側 Archetype（包含される側）
         * @return filter_R が chunk_L に包含されていれば true
         */
        static bool LhasR(const Archetype& chunk_L, const Archetype& filter_R)
        {
            // フィルタ filter_R が空なら、無条件で全チャンクが対象
            if (filter_R.size() == 0) return true;
            // チャンク chunk_L の方が少ない要素しか持たないなら絶対に包含不可
            if (chunk_L.size() < filter_R.size()) return false;

            return std::includes(
                chunk_L.begin(), chunk_L.end(),
                filter_R.begin(), filter_R.end(),
                [](const TypeData& a, const TypeData& b) {
                    return a.id < b.id;
                });
        }

        /**
         * @brief Archetype l が、除外集合 exclude のコンポーネントを「1つも含まない」かを判定する。
         *
         * Exclude（除外）フィルタの中核。System が「このコンポーネントを持つ
         * Entity は処理対象から外したい」場合に使用します。
         *
         *   - 例: l = {Position, Velocity}, exclude = {DeadTag}     → true  (DeadTagを持たない)
         *   - 例: l = {Position, DeadTag},  exclude = {DeadTag}     → false (DeadTagを持つ)
         *
         * @param l        チャンク側 Archetype
         * @param exclude  除外したいコンポーネント集合
         * @return exclude のどれも l に含まれていなければ true
         */
        static bool LExcludesR(const Archetype& l, const Archetype& exclude)
        {
            // 除外指定が無ければ常にOK（誰も弾かれない）
            if (exclude.empty()) return true;

            // exclude の各型について、l が1つでも持っていたら除外（false）
            for (const TypeData& ex : exclude)
            {
                // GetComponentIndex は O(log N) の二分探索
                if (l.GetComponentIndex(ex.id) != InvalidIndex)
                {
                    return false; // 1つでも持っていたら対象外
                }
            }
            return true; // どれも持っていなかった → 対象
        }

    };

    // ------------------------------------------------------------------
    // STL を使わない LhasR の別実装（未使用・将来の差し替え候補）
    // ------------------------------------------------------------------
    // 2本のインデックスで同時走査する O(N+M) 実装。
    // std::includes より分岐予測に乗りやすいが、実測で有意差が出なかったため採用していない。
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
