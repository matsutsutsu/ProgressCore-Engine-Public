#pragma once
// ===========================================================================
// 概要: Entity が持つコンポーネント型の組み合わせ（構成）を表す Archetype と、
//       その操作ヘルパー ArchetypeHelper
//
// 責務:
//   - コンポーネント型の集合を TypeID 昇順で保持する（Archetype）
//   - 型リストからの一括生成・包含判定・除外判定（ArchetypeHelper）
//
// 備考: Archetype は Chunk が「どんなデータを格納するか」を決める設計図にあたる
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

    // コンポーネントの型の組み合わせ。データは持たない
    // TypeData を TypeID の昇順に保つので、足した順が違っても同じ構成は一致し、一致は O(N)、包含は O(N+M) で判定できる
    class Archetype
    {
    public:
        Archetype()  = default;
        ~Archetype() = default;

        // コピー・代入は内包する std::vector<TypeData> がきちんと処理する
        Archetype(const Archetype &other)            = default;
        Archetype &operator=(const Archetype &other) = default;

        // T を足し、その位置を返す（すでにあればその位置）
        template <class T>
        size_t Add()
        {
            TypeData type;
            type.Create<T>();
            return Add(type);
        }

        // typeData を、TypeID の昇順を保つ位置に二分探索で入れる。すでにあれば何もせず、その位置を返す
        size_t Add(const TypeData &typeData)
        {
            // lower_bound は「targetId 以上の最初の要素」を指す
            // ソート順を崩さずに挿入できる位置がここになる
            auto it = std::lower_bound(
                m_data.begin(),
                m_data.end(),
                typeData.id,
                [](const TypeData &td, TypeID targetId) {
                    return td.id < targetId;
                });

            // 「以上の最初」がちょうど同じ id なら、その型は既に入っている
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

        // TypeID の要素を外す（コンポーネントを外したときの遷移で使う）
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

        // TypeID の位置を二分探索で返す。なければ InvalidIndex
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

        // 同じコンポーネント構成か。ソート済みなので O(N)
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

    // Archetype を作る・比べる関数
    class ArchetypeHelper
    {
    public:
        // 型の並びから Archetype を作る。例: ArchetypeHelper::Generate<Position, Velocity>()
        template <class... Types>
        static Archetype Generate()
        {
            Archetype a;
            // Types... を順に展開し、a.Add<Position>(), a.Add<Velocity>(), ... と呼ぶ
            (a.Add<Types>(), ...);
            return a;
        }

        // chunk_L が filter_R の型をすべて持つか（System の Filter が Chunk に合うかの判定）
        // 両方ソート済みなので、std::includes で O(N+M)
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

        // l が exclude の型を1つも持たないか（除外フィルタの判定）
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

}
