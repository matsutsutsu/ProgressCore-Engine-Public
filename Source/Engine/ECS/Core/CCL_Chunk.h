#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <functional>
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/Common/CCL_Types.h"

// Chunk（チャンク） は、特定のアーキタイプ（コンポーネントの組み合わせ）
// に属するエンティティのデータを、メモリ上にまとめて格納する「データの箱」
// 実際ではこのChunkが複数生成されてその個別のChunkのクラス
#pragma region Chunk概要説明

//  1. Chunk の役割（概要）
//  通常のオブジェクト指向（AoS : Array of Structures）では、
//  1つの敵オブジェクトの中に座標、速度、HPなどがバラバラに保持されます。
//  しかし、この ECS 設計の Chunk では SoA(Structure of Arrays) を採用しています。
//
//  同一構造の集積 : 同じコンポーネント構成（例：Position と Velocity を持つ）
//  を持つエンティティだけを 1 つの Chunk に集めます。
//
//  メモリ効率とキャッシュ : コンポーネントの種類ごとに
//  連続したメモリ領域を確保するため、CPU キャッシュ効率が極めて高く、
//  高速なループ処理が可能です。
//
//
//  2. 主要な変数（データの持ち方）
//  Chunk が内部でどのようなデータを管理しているかを見てみましょう。
//
//  m_archetype(Archetype) : この Chunk が「何のコンポーネントを格納しているか」
//  という定義情報です。
//
//  m_components(std::vector<std::vector<uint8_t>>) : Chunk の中身（実データ）です。
//
//  外側の vector は「コンポーネントの種類」の数だけあります。
//
//  内側の vector は「全エンティティのそのコンポーネントの生データ」が
//  ギッシリ詰まった連続メモリです。
//
//  m_ownerEntities(std::vector<EntityID>) :
//  この Chunk に入っている各スロットが、どの EntityID のものかを記録する配列です。
//
//  m_entityToIndex(std::unordered_map<EntityID, size_t>) :
// 「EntityID」から「そのデータが Chunk 内の何番目にあるか」を高速に逆引きするための辞書です。
//
//  m_destroyedEntities(std::vector<size_t>) :
//  削除予約されたエンティティのインデックスを一時的に貯めておくリストです。
//
//  3. 主要な関数（何ができるのか）
//  Chunk を操作するための主要な機能です。
//
//  データの追加と取得
//  Create : 指定されたアーキタイプに基づいて、メモリ領域を初期確保します。
//
//  InsertEntity : 新しいエンティティ用の場所を確保し、
//  その書き込み先ポインタ（生メモリ）を返します。
//  足りなければ自動でメモリを拡張します。
//
//  GetComponentPtr : 「何番目のエンティティの、どのコンポーネント」が
//  欲しいかを指定すると、その正確なメモリ番地を計算して返します。
//
//  データの削除（2段階）
//  MarkDestroyEntity : すぐにデータを消さず、「この番号は後で消す」
//  という印をつけるだけです。
//
//  CompactHoles : 「印」がついた箇所を詰め、メモリを隙間なく並べ替えます
// （これが ECS の高速性を維持する秘訣です）。
//
//  後片付け
//  Finalize : Chunk 内の全エンティティに対し、
//  それぞれの型に合ったデストラクタを正しく呼び出して、メモリを完全に解放します。
//
//  まとめると
//  Chunk は 「同じ型セットを持つエンティティたちを、
//  コンポーネントごとの連続した列（SoA）に並べて保持する固定サイズの領域」。

#pragma endregion

namespace CCL::ECS
{
    using namespace CCL::ECS;

    class Chunk
    {
    public:
        Chunk() = default;
        ~Chunk();

        // コピー禁止
        Chunk(const Chunk &)            = delete;
        Chunk &operator=(const Chunk &) = delete;

        // ムーブ禁止
        Chunk(Chunk &&)            = delete;
        Chunk &operator=(Chunk &&) = delete;

        // 指定されたアーキタイプに基づいてChunkを初期化
        //   第１引数 : このChunkが保持するコンポーネントの種類とサイズ情報
        //   第２引数 : 最初に確保するエンティティ数のキャパシティ
        void Create(const Archetype &archetype, size_t initialEntityCapacity = 16);
        void CreateNull(size_t initialEntityCapacity = 16);
        // Chunk内の全エンティティとデータを破棄する
        void Finalize();

        // 新しいエンティティを挿入し、そのコンポーネントデータの先頭ポインタを返す
        void *InsertEntity(EntityID entity);
        // Remove entity by index (call destructors only)
        void RemoveEntity(size_t entityIndex);
        // 引数のEntityをすぐにデータを消さず、「この番号は後で消す」という印をつける
        void MarkDestroyEntity(size_t entityIndex);

        size_t           GetEntityCount() const { return m_entityCount; }
        size_t           GetChunkSize() const { return m_entityStride; }
        const Archetype &GetArchetype() const { return m_archetype; }
        EntityID         GetEntityByIndex(size_t i) const { return m_ownerEntities[i]; }

        // 削除の「印」がついた箇所を詰め、メモリを隙間なく並べ替える
        // ECSの高速性を維持する秘訣
        // 引数にコールバックを追加
        void CompactHoles(const std::function<void(EntityID, Chunk *, size_t)> &onMoveCallback);

        // チャンクの最大収容数を返す
        size_t GetCapacity() const { return m_capacity; }

        // デバッグ用：アーキタイプの簡易ハッシュ
        size_t GetArchetypeHash() const
        {
            size_t hash = 0;
            for (const auto &t : m_archetype)
            {
                hash ^= std::hash<uint32_t>{}(t.id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }

        // 戻り値を size_t (挿入されたインデックス) に変更
        size_t AddEntity(EntityID entity);

        bool IsEntityDestroyed(size_t index) const
        {
            // ? ここどういった処理なのか
            return std::find(m_destroyedEntities.begin(), m_destroyedEntities.end(), index) !=
                   m_destroyedEntities.end();
        }

        /**
         * @brief Chunkに格納されている全EntityIDの配列の先頭ポインタを取得します。
         * System側で「自分自身のEntityID」を知る必要がある場合（ScriptSystem等）に使用します。
         * @return const EntityID* ID配列の先頭ポインタ
         */
        const EntityID *GetEntityIDs() const
        {
            if (m_ownerEntities.empty()) return nullptr;
            return m_ownerEntities.data();
        }

        // 削除リストが空でないか（削除予定のエンティティがあるか）を返す
        bool HasDestroyedEntities() const { return !m_destroyedEntities.empty(); }

    private:
        // このChunkが「なんのコンポーネント」を持っているかの定義情報
        Archetype m_archetype;
        size_t    m_capacity = 0; // 最大容量を保持

        // このChunkに所属するエンティティIDのリスト
        std::vector<EntityID> m_ownerEntities;
        size_t                m_entityStride = 0;
        size_t                m_entityCount  = 0;

        // 削除予定のエンティティインデックスリスト
        std::vector<size_t> m_destroyedEntities;

        // Chunkの中身（実データ）。
        //   1本の連続メモリに種類ごとの列を並べた SoA。各列の先頭位置は
        //   m_componentOffsets が持つ。種類ごとに vector を分けないのは、
        //   走査が1本の連続領域で済むようにするため。
        std::vector<uint8_t> m_memoryBlock;
        std::vector<size_t>  m_componentOffsets;

    public:
        size_t GetComponentIndex(TypeID id) const
        {
            for (size_t i = 0; i < m_archetype.size(); ++i)
                if (m_archetype[i].id == id)
                    return i;
            return InvalidIndex;
        }

        // 渡されたidとentityIndexからSoA配列内の正確なメモリアドレスを計算する
        // 返り値を返すときChunk自身はintなのかstringなのかは知らないので
        // 型が不明な住所のvoid*として返す
        void *GetComponentPtrByType(TypeID id, size_t entityIndex)
        {
            // 1. Componentのインデックスを取得 (GetComponentIndex)
            size_t compIdx = GetComponentIndex(id);

            // 2. このChunkが指定されたComponent型を持っていない場合は終了
            if (compIdx == InvalidIndex) return nullptr;

            // 3. ComponentPtrを計算して返す
            return GetComponentPtr(compIdx, entityIndex);
        }

        const Archetype &GetTypeDatas() const // メソッド名はそのままに、戻り値を Archetype& に修正
        {
            return m_archetype;
        }

        // 指定されたTypeIDのコンポーネントを持っているか？
        bool HasComponent(TypeID id) const
        {
            return GetComponentIndex(id) != InvalidIndex;
        }

        // ---------------------------------------------------------------------
        // テンプレート版アクセサ (ユーザーコード用)
        // ---------------------------------------------------------------------

        // 指定された型Tのコンポーネントを持っているか判定
        template <class T>
        bool HasComponent() const
        {
            // TypeInfo<T>::ID() で型IDを取得して判定
            return GetComponentIndex(TypeInfo<T>::ID()) != InvalidIndex;
        }

        // 指定された型Tのコンポーネントデータ配列の先頭ポインタを取得
        // 戻り値: T* (持っていない場合は nullptr)
        template <class T>
        T *GetComponentArray()
        {
            // 1. 型IDに対応するコンポーネント配列のインデックス（何番目の白線か）を取得
            size_t index = GetComponentIndex(TypeInfo<T>::ID());

            // 2. 存在しなければ nullptr を返す
            if (index == InvalidIndex) return nullptr;

            // 3. 体育館の入り口から、目的の白線の位置まで進んだ場所を T* にキャストして返す
            return reinterpret_cast<T *>(m_memoryBlock.data() + m_componentOffsets[index]);
        }

        // const版 GetComponentArray (読み取り専用)
        template <class T>
        const T *GetComponentArray() const
        {
            size_t index = GetComponentIndex(TypeInfo<T>::ID());

            if (index == InvalidIndex) return nullptr;

            return reinterpret_cast<const T *>(m_memoryBlock.data() + m_componentOffsets[index]);
        }

    private:
        // 「何番目のエンティティの、どのコンポーネント」が欲しいかを指定すると、
        // 　その正確なメモリ番地を計算して返します
        // 第１引数 : コンポーネントのインデックス（m_archetype 内の位置）
        // 第２引数 : エンティティのインデックス（Chunk内の位置）
        void *GetComponentPtr(size_t compIdx, size_t entityIndex)
        {
            // 1. 指定されたコンポーネントの配列を取り出す
            size_t baseOffset = m_componentOffsets[compIdx];
            // 2. その型の1個あたりのサイズを取得
            size_t sz = m_archetype[compIdx].typeSize;
            // 3. 配列の先頭アドレス + (何番目か * サイズ) で場所を特定
            // 体育館の入り口(m_memoryBlock) + エリアの開始位置(baseOffset) + 人数分進む
            return m_memoryBlock.data() + baseOffset + (entityIndex * sz);
        }
    };

} // namespace CCL::ECS
