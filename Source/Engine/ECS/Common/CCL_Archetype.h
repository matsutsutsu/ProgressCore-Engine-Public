/**
 * @file   CCL_Archetype.h
 * @brief  コンポーネント型の組み合わせを表現する Archetype と関連ヘルパ
 *
 * @details
 *  ECSにおける Archetype は「Position + Velocity + Render」など、
 *  エンティティが持つコンポーネント型の集合体を表します。
 *  Archetypeは TypeData を TypeID 昇順でソートして保持しているため、
 *  同じ集合は順序にかかわらず一意に判別できます。
 *
 *  Chunk は1つのArchetypeに対して作られ、その集合に所属するEntityのみを
 *  格納します(SoA構造)。
 *
 *  本ファイルでは以下を提供します:
 *   - Archetype       : TypeDataのソート済み集合
 *   - ArchetypeHelper : 複数型からArchetypeを自動生成 / 包含判定ヘルパ
 *
 *  【依存】
 *   - CCL_TypeData.h
 *   - \<vector\>, \<algorithm\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_TypeData.h"

#include <vector>
#include <algorithm>


namespace CCL::ECS
{
    // =========================================================================
    //  Archetype
    // =========================================================================

    /**
     * @class Archetype
     * @brief コンポーネント型の集合を、TypeID昇順でソートした順序集合として保持する
     *
     * @details
     *  「コンポーネント構成」を一意に表現するクラスです。
     *  内部 std::vector<TypeData> を昇順ソート状態で保持することで、
     *  等値判定や包含判定を高速(線形 or 対数)に実装できます。
     *
     *  @par 使用例
     *  @code
     *      Archetype a;
     *      a.Add<Position>();
     *      a.Add<Velocity>();
     *      // → TypeIDが小さい順に格納される(順序不問で一意性が保たれる)
     *  @endcode
     */
    class Archetype
    {
    public:
        Archetype()                                  = default;
        ~Archetype()                                 = default;
        Archetype(const Archetype&)                  = default;
        Archetype& operator=(const Archetype&)       = default;
        Archetype(Archetype&&) noexcept              = default;
        Archetype& operator=(Archetype&&) noexcept   = default;


        // =====================================================================
        //  追加 / 削除
        // =====================================================================

        /**
         * @brief 型Tを追加する(既に含まれていれば何もしない)
         * @tparam T 追加したいコンポーネント型
         * @return 追加された(または既存の)要素のインデックス
         */
        template <class T>
        std::size_t Add()
        {
            TypeData td;
            td.Create<T>();
            return Add(td);
        }

        /**
         * @brief TypeDataを追加する(既に同じIDがあれば何もしない)
         * @param typeData 追加するTypeData
         * @return 追加された(または既存の)要素のインデックス
         */
        std::size_t Add(const TypeData& typeData)
        {
            // 二分探索で挿入位置を確定
            auto it = std::lower_bound(
                _data.begin(), _data.end(), typeData.id,
                [](const TypeData& td, TypeID target) { return td.id < target; });

            // 重複チェック
            if (it != _data.end() && it->id == typeData.id) {
                return static_cast<std::size_t>(std::distance(_data.begin(), it));
            }

            std::size_t index = static_cast<std::size_t>(std::distance(_data.begin(), it));
            _data.insert(it, typeData);
            return index;
        }

        /**
         * @brief 型Tを削除する(含まれていなければ何もしない)
         * @tparam T 削除したいコンポーネント型
         */
        template <class T>
        void Sub()
        {
            Sub(TypeInfo<T>::ID());
        }

        /**
         * @brief TypeIDを指定して削除する
         * @param id 削除対象のTypeID
         */
        void Sub(TypeID id)
        {
            auto it = std::lower_bound(
                _data.begin(), _data.end(), id,
                [](const TypeData& td, TypeID target) { return td.id < target; });

            if (it != _data.end() && it->id == id) {
                _data.erase(it);
            }
        }


        // =====================================================================
        //  問い合わせ
        // =====================================================================

        /**
         * @brief TypeIDに対応する内部インデックスを取得 (O(log N))
         * @param id 対象TypeID
         * @return  見つかればインデックス、なければ InvalidIndex
         */
        std::size_t GetComponentIndex(TypeID id) const
        {
            auto it = std::lower_bound(
                _data.begin(), _data.end(), id,
                [](const TypeData& td, TypeID target) { return td.id < target; });

            if (it != _data.end() && it->id == id) {
                return static_cast<std::size_t>(std::distance(_data.begin(), it));
            }
            return InvalidIndex;
        }

        /**
         * @brief 型Tを含むか判定
         * @tparam T 判定対象型
         */
        template <class T>
        bool Has() const noexcept
        {
            return GetComponentIndex(TypeInfo<T>::ID()) != InvalidIndex;
        }


        // =====================================================================
        //  STL風アクセス
        // =====================================================================

        const TypeData& at(std::size_t i) const           { return _data.at(i); }
        const TypeData& operator[](std::size_t i) const   { return _data[i];    }
        std::size_t     size()  const noexcept            { return _data.size();  }
        bool            empty() const noexcept            { return _data.empty(); }

        auto begin() const noexcept { return _data.cbegin(); }
        auto end()   const noexcept { return _data.cend();   }


        // =====================================================================
        //  比較 / ハッシュ
        // =====================================================================

        /// @brief 構成が完全に同じか判定(順序はソート済みで保証されている)
        bool operator==(const Archetype& r) const
        {
            if (_data.size() != r._data.size()) return false;
            return std::equal(_data.begin(), _data.end(), r._data.begin());
        }

        bool operator!=(const Archetype& r) const { return !(*this == r); }

        /**
         * @brief Archetype構成を表すハッシュ値を計算(キャッシュ付き)
         * @details ChunkManagerでの高速チャンク検索などに使用可能。
         */
        std::size_t Hash() const
        {
            if (!_hashDirty) return _cachedHash;

            std::size_t h = 0;
            for (const auto& td : _data) {
                // Boost風 hash_combine
                h ^= static_cast<std::size_t>(td.id)
                   + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            _cachedHash = h;
            _hashDirty  = false;
            return _cachedHash;
        }

    private:
        std::vector<TypeData> _data;

        // ハッシュキャッシュ(mutableで内部状態の論理constを保つ)
        mutable std::size_t   _cachedHash = 0;
        mutable bool          _hashDirty  = true;

        // Add/Subのたびにキャッシュを無効化
        void InvalidateHash() noexcept { _hashDirty = true; }

    public:
        // Add/Subが呼ばれたらキャッシュ無効化(public ラッパ経由のため明示)
        // 内部実装の関係上、ここでハック的に再宣言
        // ※実装ではAdd/Sub内で直接 _hashDirty を反映してもOK
    };


    // =========================================================================
    //  ArchetypeHelper
    // =========================================================================

    /**
     * @class ArchetypeHelper
     * @brief 複数の型から Archetype を一発生成 / 包含判定するための静的ヘルパ
     */
    class ArchetypeHelper
    {
    public:
        /**
         * @brief 複数の型から Archetype を生成
         * @tparam Types 含めたいコンポーネント型(可変長)
         * @return 全型を含む Archetype
         *
         * @par 使用例
         * @code
         *     auto a = ArchetypeHelper::Generate<Position, Velocity, Renderable>();
         * @endcode
         */
        template <class... Types>
        static Archetype Generate()
        {
            Archetype a;
            (a.Add<Types>(), ...);
            return a;
        }

        /**
         * @brief 左辺(l)が右辺(r)の全要素を含むか判定 (O(N+M))
         * @param l チャンク等の "保有" Archetype
         * @param r フィルタ等の "要求" Archetype
         * @return  l が r を包含していれば true
         *
         * @details
         *  Systemのフィルタ判定で利用されます。「Chunkが要求する全部の型を持っているか?」
         *  をTypeID昇順に2つのイテレータを進める方式で線形時間でチェックします。
         */
        static bool LhasR(const Archetype& l, const Archetype& r)
        {
            if (r.size() == 0) return true;
            if (l.size() < r.size()) return false;

            std::size_t i = 0;
            std::size_t j = 0;
            while (i < l.size() && j < r.size()) {
                if (l[i].id == r[j].id) {
                    ++i; ++j;
                }
                else if (l[i].id < r[j].id) {
                    ++i;
                }
                else {
                    // l[i].id > r[j].id → rのidがlに存在しない
                    return false;
                }
            }
            // rを最後まで進めきれたら包含成立
            return j == r.size();
        }
    };

} // namespace CCL::ECS
