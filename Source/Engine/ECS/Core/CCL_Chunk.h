#pragma once
// ===========================================================================
// 概要: 同じ Archetype（コンポーネント構成）の Entity を SoA でまとめて格納する、定員固定のデータ領域
//
// 責務:
//   - 定員分のメモリを Create で1回だけ確保し、以降は確保しない
//   - スロットの確保（AddEntity / InsertEntity）とコンポーネントの番地の計算
//   - 2段階の削除（MarkDestroyEntity で印を付け、CompactHoles で詰める）
//   - Finalize で型ごとのデストラクタを呼んで破棄する
//
// 備考: 1体ごとにデータを並べる（AoS）のではなく、コンポーネントの種類ごとの列を並べる（SoA）
//         m_memoryBlock（1本の連続メモリ）
//         ┌──────────────────┬──────────────────┬───
//         │ Position × 定員   │ Velocity × 定員   │ ...
//         └──────────────────┴──────────────────┴───
//          ↑m_componentOffsets[0]  ↑m_componentOffsets[1]
//       同じ種類のデータが隙間なく並ぶので、ForEach で1種類ずつ走査するとキャッシュに乗りやすい
// ===========================================================================
#include <vector>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <functional>
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::ECS
{
    class Chunk
    {
    public:
        Chunk() = default;
        ~Chunk();

        // System が Chunk* を握るので、アドレスが変わるコピー・ムーブは禁止
        Chunk(const Chunk &)            = delete;
        Chunk &operator=(const Chunk &) = delete;
        Chunk(Chunk &&)                 = delete;
        Chunk &operator=(Chunk &&)      = delete;

        // ============================================================
        // 生成・破棄
        // ============================================================

        // Archetype 用に定員分のメモリをまとめて確保する。その構成の Chunk がまだないときに1回だけ呼ぶ
        // 定員は ChunkManager::CalculateChunkCapacity() が 16KB ÷ 1体のバイト数で決める
        void Create(const Archetype &archetype, size_t initialEntityCapacity = 16);

        // 生きている全 Entity のデストラクタを呼び、メモリを解放する。2回呼んでもよい
        void Finalize();

        // ============================================================
        // Entity の追加・削除
        // ============================================================

        // スロットを1つ確保して EntityID を書く。メモリの確保は起きない。満員なら assert して InvalidIndex
        size_t AddEntity(EntityID entity);

        // AddEntity に加え、確保したスロットの全コンポーネントをゼロで埋める
        // AddEntity だけだと、前にそのスロットにいた Entity のデータが残る
        void *InsertEntity(EntityID entity);

        // 後で消すスロットに印を付ける。デストラクタは呼ばないので、呼び出し側が先に呼んでおく
        // 実際に詰めるのはフレーム境界の CompactHoles
        void MarkDestroyEntity(size_t entityIndex);

        // 印の付いたスロットを詰め、生きている Entity を前へ寄せる
        // Entity が動くたびに onMoveCallback を呼ぶ。ChunkManager はこれで住所録を直す（直さないと古いスロットを指す）
        void CompactHoles(const std::function<void(EntityID, Chunk *, size_t)> &onMoveCallback);

        // スロットが削除予約済みか。削除リストを線形探索する（件数は通常ごく少ない）
        bool IsEntityDestroyed(size_t index) const
        {
            return std::find(m_destroyedEntities.begin(), m_destroyedEntities.end(), index) !=
                   m_destroyedEntities.end();
        }

        // 削除予約済みの Entity が1体でもあるか。ForEach はこれが false なら IsEntityDestroyed を省く
        bool HasDestroyedEntities() const { return !m_destroyedEntities.empty(); }

        // ============================================================
        // 情報取得
        // ============================================================

        size_t GetEntityCount() const { return m_entityCount; }

        // 1 Entity あたりのバイト数（名前に反して Chunk 全体のサイズではない）
        size_t GetChunkSize() const { return m_entityStride; }

        size_t GetCapacity() const { return m_capacity; } // 定員

        const Archetype &GetArchetype() const { return m_archetype; }

        EntityID GetEntityByIndex(size_t i) const { return m_ownerEntities[i]; }

        // Chunk 内の全 EntityID の配列の先頭を返す（ForEachWithID などで使う）
        const EntityID *GetEntityIDs() const
        {
            if (m_ownerEntities.empty()) return nullptr;
            return m_ownerEntities.data();
        }

        // デバッグ用: Archetype の簡易ハッシュ（エディタで同じ構成の Chunk をまとめるのに使う）
        size_t GetArchetypeHash() const
        {
            size_t hash = 0;
            for (const auto &t : m_archetype)
            {
                hash ^= std::hash<uint32_t>{}(t.id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }

    private:
        Archetype m_archetype;    // この Chunk が持つコンポーネント構成
        size_t    m_capacity = 0; // 定員（Create 時に確定）

        std::vector<EntityID> m_ownerEntities;    // スロット番号 → EntityID の名簿（空きは InvalidEntityID）
        size_t                m_entityStride = 0; // 1 Entity あたりのバイト数（全コンポーネントの合計）
        size_t                m_entityCount  = 0; // 使用中のスロット数（削除予約分も含む）

        std::vector<size_t> m_destroyedEntities; // 削除予約されたスロット番号

        // Chunk の実データ
        //   1本の連続メモリに種類ごとの列を並べた SoA。各列の先頭位置は
        //   m_componentOffsets が持つ。種類ごとに vector を分けないのは、
        //   走査が1本の連続領域で済むようにするため
        std::vector<uint8_t> m_memoryBlock;
        std::vector<size_t>  m_componentOffsets;

    public:
        // ============================================================
        // コンポーネントへのアクセス
        // ============================================================

        // Archetype 内での位置を返す。持っていなければ InvalidIndex
        size_t GetComponentIndex(TypeID id) const
        {
            for (size_t i = 0; i < m_archetype.size(); ++i)
                if (m_archetype[i].id == id)
                    return i;
            return InvalidIndex;
        }

        // TypeID とスロット番号から、SoA 内のメモリ番地を返す。この Chunk がその型を持たなければ nullptr
        // Chunk は中身の型を知らないので、void* で返す
        void *GetComponentPtrByType(TypeID id, size_t entityIndex)
        {
            size_t compIdx = GetComponentIndex(id);

            if (compIdx == InvalidIndex) return nullptr;

            return GetComponentPtr(compIdx, entityIndex);
        }

        // GetArchetype と同じものを返す（エディタの ECS デバッグウィンドウが使う）
        const Archetype &GetTypeDatas() const
        {
            return m_archetype;
        }

        bool HasComponent(TypeID id) const
        {
            return GetComponentIndex(id) != InvalidIndex;
        }

        template <class T>
        bool HasComponent() const
        {
            return GetComponentIndex(TypeInfo<T>::ID()) != InvalidIndex;
        }

        // 型 T の列の先頭を返す。持っていなければ nullptr
        template <class T>
        T *GetComponentArray()
        {
            size_t index = GetComponentIndex(TypeInfo<T>::ID());

            if (index == InvalidIndex) return nullptr;

            // メモリブロックの先頭から、T の列の開始位置まで進めて T* として返す
            return reinterpret_cast<T *>(m_memoryBlock.data() + m_componentOffsets[index]);
        }

        // GetComponentArray の読み取り専用版
        template <class T>
        const T *GetComponentArray() const
        {
            size_t index = GetComponentIndex(TypeInfo<T>::ID());

            if (index == InvalidIndex) return nullptr;

            return reinterpret_cast<const T *>(m_memoryBlock.data() + m_componentOffsets[index]);
        }

    private:
        // m_archetype 内のコンポーネントの位置と、スロット番号から番地を返す
        void *GetComponentPtr(size_t compIdx, size_t entityIndex)
        {
            // メモリブロックの先頭 + 列の開始位置 + (スロット番号 × 1個のサイズ)
            size_t baseOffset = m_componentOffsets[compIdx];
            size_t sz         = m_archetype[compIdx].typeSize;
            return m_memoryBlock.data() + baseOffset + (entityIndex * sz);
        }
    };

} // namespace CCL::ECS
