#pragma once
// ===========================================================================
// 概要: Entity が持つコンポーネント型の組み合わせ（構成）を表す Archetype と、
//       その操作ヘルパー ArchetypeHelper。
//
// 責務:
//   - コンポーネント型の集合を TypeID 昇順で保持する（Archetype）
//   - 型リストからの一括生成・包含判定・除外判定（ArchetypeHelper）
//
// 備考: Archetype は Chunk が「どんなデータを格納するか」を決める設計図にあたる。
// ===========================================================================

#include <vector>
#include <algorithm>

#include "CCL_Types.h"
#include "CCL_TypeInfo.h"

namespace CCL::ECS
{
    // ============================================================
    // Archetype
    // ============================================================

    /**
     * @brief コンポーネント型の組み合わせ（構成）を表すクラス。
     *
     * Archetype はあくまで「型の集合」であり、実体データは持たない。
     * 内部の `std::vector<TypeData>` を常に TypeID 昇順に保つので、
     *   - 一致判定が O(N)
     *   - 包含判定が O(N+M)
     * で済む（同じ構成なら追加した順番が違ってもソート後に一致する）。
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
        Archetype(const Archetype &other)            = default;
        Archetype &operator=(const Archetype &other) = default;

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
         * `std::lower_bound` の二分探索（O(log N)）で挿入位置を決めて挿入する。
         * 既に同じ型があれば何もせず、その位置を返す。
         *
         * @return 追加先（または既存）のインデックス
         */
        size_t Add(const TypeData &typeData)
        {
            // lower_bound は「targetId 以上の最初の要素」を指す。
            // ソート順を崩さずに挿入できる位置がここになる
            auto it = std::lower_bound(
                m_data.begin(),
                m_data.end(),
                typeData.id,
                [](const TypeData &td, TypeID targetId) {
                    return td.id < targetId;
                });

            // 「以上の最初」がちょうど同じ id なら、その型は既に入っている。
            // 重複して入れないよう、追加せずにその位置を返す
            // （it が end なら、全要素が targetId より小さい＝末尾に追加する）
            if (it != m_data.end() && it->id == typeData.id)
            {
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
         * Component が外されたときの Archetype 遷移で使う。
         * Add と同じく二分探索（O(log N)）で探してから erase する。
         */
        void Sub(TypeID id)
        {
            auto it = std::lower_bound(
                m_data.begin(),
                m_data.end(),
                id,
                [](const TypeData &td, TypeID targetId) {
                    return td.id < targetId;
                });

            if (it != m_data.end() && it->id == id)
            {
                m_data.erase(it);
            }
        }

        // ============================================================
        // 検索・比較
        // ============================================================

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
                [](const TypeData &td, TypeID targetId) {
                    return td.id < targetId;
                });

            if (it != m_data.end() && it->id == id)
            {
                return std::distance(m_data.begin(), it);
            }
            return InvalidIndex;
        }

        /**
         * @brief 二つの Archetype が「同じコンポーネント構成」かを判定する。
         * @note TypeData が常にソート済みのため O(N) で高速比較できる。
         */
        bool operator==(const Archetype &r) const
        {
            if (m_data.size() != r.m_data.size()) return false;
            return std::equal(m_data.begin(), m_data.end(), r.m_data.begin());
        }

        // ============================================================
        // 読み取り専用アクセサ
        // ============================================================

        const TypeData &at(size_t i) const { return m_data.at(i); }
        const TypeData &operator[](size_t i) const { return m_data[i]; }

        size_t size() const { return m_data.size(); }
        bool   empty() const { return m_data.empty(); }

        // 範囲 for で回せるようにする
        std::vector<TypeData>::const_iterator begin() const { return m_data.cbegin(); }
        std::vector<TypeData>::const_iterator end() const { return m_data.cend(); }

    private:
        std::vector<TypeData> m_data; // 常に TypeID 昇順（不変条件）
    };

    // ============================================================
    // ArchetypeHelper
    // ============================================================

    /**
     * @brief Archetype を扱うためのユーティリティ関数群。
     *
     * 「型パラメータパックから Archetype を一発生成」「包含判定」などを切り出したもの。
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
         * 内部で `(a.Add<Types>(), ...);` という C++17 の fold 式を使い、
         * Add を型の数だけ展開している。
         */
        template <class... Types>
        static Archetype Generate()
        {
            Archetype a;
            // Types... を順に展開し、a.Add<Position>(), a.Add<Velocity>(), ... と呼ぶ
            (a.Add<Types>(), ...);
            return a;
        }

        /**
         * @brief Archetype chunk_L が Archetype filter_R を「全て包含している」かを判定する。
         *
         * System の Filter が Chunk に合うかの判定に使う。
         *   - 例: chunk_L = [Position, Velocity, Render], filter_R = [Position, Velocity] → true
         *   - 例: chunk_L = [Position, Velocity], filter_R = [Position, Velocity, Render] → false
         *
         * 両者ソート済みなので `std::includes` で O(N+M) で判定できる。
         *
         * @param chunk_L  チャンク側 Archetype（包含する側）
         * @param filter_R  フィルタ側 Archetype（包含される側）
         * @return filter_R が chunk_L に包含されていれば true
         *
         * @note 2本のインデックスで同時に走査する自前実装も試したが、
         *       実測で std::includes と有意差が出なかったので採用していない。
         */
        static bool LhasR(const Archetype &chunk_L, const Archetype &filter_R)
        {
            // フィルタ filter_R が空なら、無条件で全チャンクが対象
            if (filter_R.size() == 0) return true;
            // chunk_L の方が要素が少なければ、包含はありえない
            if (chunk_L.size() < filter_R.size()) return false;

            return std::includes(
                chunk_L.begin(), chunk_L.end(),
                filter_R.begin(), filter_R.end(),
                [](const TypeData &a, const TypeData &b) {
                    return a.id < b.id;
                });
        }

        /**
         * @brief Archetype l が、除外集合 exclude のコンポーネントを「1つも含まない」かを判定する。
         *
         * 除外フィルタの中核。System が「このコンポーネントを持つ Entity は
         * 処理対象から外したい」場合に使う。
         *
         *   - 例: l = {Position, Velocity}, exclude = {DeadTag}     → true  (DeadTagを持たない)
         *   - 例: l = {Position, DeadTag},  exclude = {DeadTag}     → false (DeadTagを持つ)
         *
         * @param l        チャンク側 Archetype
         * @param exclude  除外したいコンポーネント集合
         * @return exclude のどれも l に含まれていなければ true
         */
        static bool LExcludesR(const Archetype &l, const Archetype &exclude)
        {
            if (exclude.empty()) return true;

            for (const TypeData &ex : exclude)
            {
                if (l.GetComponentIndex(ex.id) != InvalidIndex)
                {
                    return false;
                }
            }
            return true;
        }
    };

} // namespace CCL::ECS
