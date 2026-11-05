#include "CCL_Chunk.h"
#include <algorithm>
#include <cstring>
#include <cassert>

namespace CCL::ECS
{
    // size を alignment の倍数へ切り上げる（alignment は 2 のべき乗）
    inline size_t AlignUp(size_t size, size_t alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }



    Chunk::~Chunk()
    {
        // 二重解放の防止は Finalize 側が持つ

        Finalize();
    }



    void Chunk::Create(const Archetype &archetype, size_t initialEntityCapacity)
    {
        // 列の幅は今いる数でなく定員で決める。8体しかいなくても、各列は定員ぶんの幅を持つ
        // 先に定員ぶんを確保するので、Entity を足してもメモリの確保は起きない

        m_archetype    = archetype;
        m_capacity     = initialEntityCapacity;
        m_entityStride = 0;
        for (const TypeData &t : m_archetype)
            m_entityStride += t.typeSize;

        m_ownerEntities.assign(initialEntityCapacity, InvalidEntityID);

        // 各列の開始位置を決めてから、合計サイズで1回だけ確保する
        size_t compCount = m_archetype.size();
        m_componentOffsets.resize(compCount);

        size_t       currentOffset = 0;
        const size_t ALIGNMENT     = 16; // 列の先頭をそろえる境界（バイト）

        for (size_t i = 0; i < compCount; ++i)
        {
            // 1. 列の開始位置を16の倍数に切り上げる
            currentOffset = AlignUp(currentOffset, ALIGNMENT);

            // 2. この列の開始位置を記録する
            m_componentOffsets[i] = currentOffset;

            // 3. 列の幅（1個のサイズ × 定員）だけ進める
            currentOffset += m_archetype[i].typeSize * m_capacity;
        }

        // 4. 必要な総バイト数で、メモリを1回だけ確保する
        m_memoryBlock.resize(currentOffset);

        m_entityCount = 0;
        m_destroyedEntities.clear();
    }



    void Chunk::Finalize()
    {
        // 未作成か Finalize 済み。二重に解放しないよう何もしない
        if (m_componentOffsets.empty()) return;

        // 1. 生きている要素だけ、TypeData のデストラクタで破棄する
        //    生のメモリなので、型ごとの関数ポインタがないと正しく破棄できない
        for (size_t ci = 0; ci < m_archetype.size(); ++ci)
        {
            size_t sz         = m_archetype[ci].typeSize;
            size_t baseOffset = m_componentOffsets[ci];

            for (size_t i = 0; i < m_entityCount; ++i)
            {
                if (!IsEntityDestroyed(i))
                {
                    void *ptr = m_memoryBlock.data() + baseOffset + (i * sz);
                    m_archetype[ci].destructor.Destruct(ptr);
                }
            }
        }

        // 2. 全コンテナを空にしてメモリを返す
        m_memoryBlock.clear();
        m_componentOffsets.clear();
        m_ownerEntities.clear();
        m_destroyedEntities.clear();
        m_entityCount = 0;
    }



    void *Chunk::InsertEntity(EntityID entity)
    {
        size_t writeIndex = AddEntity(entity);

        // 前にこのスロットにいた Entity のデータが残っているので、ゼロで埋める
        for (size_t ci = 0; ci < m_archetype.size(); ++ci)
        {
            void *dst = GetComponentPtr(ci, writeIndex);
            std::memset(dst, 0, m_archetype[ci].typeSize);
        }
        return GetComponentPtr(0, writeIndex);
    }



    void Chunk::MarkDestroyEntity(size_t entityIndex)
    {
        // 印を付けるだけで、詰めるのはフレームの境目の CompactHoles
        // ForEach の途中でスロットが詰まると、ループが持っているポインタと添字が壊れる

        if (entityIndex >= m_entityCount) return;

        m_ownerEntities[entityIndex] = InvalidEntityID;
        m_destroyedEntities.push_back(entityIndex);
    }



    void Chunk::CompactHoles(const std::function<void(EntityID, Chunk *, size_t)> &onMoveCallback)
    {
        // 前から read で全部見て、生きている要素を write の位置へ詰める
        // 例: [A, ×, B, ×, C] は [A, B, C] になり、m_entityCount は 3
        // 移動は mover があればムーブ、なければ memmove。ムーブした元はデストラクタで始末する

        if (m_destroyedEntities.empty()) return;

        size_t read  = 0;
        size_t write = 0;

        while (read < m_entityCount)
        {
            EntityID currentID = m_ownerEntities[read];

            bool isDead = (currentID == InvalidEntityID);

            if (isDead)
            {
                // 破棄は呼び出し元（ApplyDestroyOp など）で済んでいるので、穴を飛ばすだけ
                ++read;
                continue;
            }

            if (read != write)
            {
                m_ownerEntities[write] = currentID;

                // 各コンポーネントのデータを、後ろのスロット(read)から前のスロット(write)へ移す
                for (size_t ci = 0; ci < m_archetype.size(); ++ci)
                {
                    size_t baseOffset = m_componentOffsets[ci];
                    void  *dst =
                        m_memoryBlock.data() + baseOffset + (write * m_archetype[ci].typeSize);
                    void *src = m_memoryBlock.data() + baseOffset + (read * m_archetype[ci].typeSize);

                    if (m_archetype[ci].mover)
                    {
                        m_archetype[ci].mover(dst, src);
                    }
                    else
                    {
                        std::memmove(dst, src, m_archetype[ci].typeSize);
                    }

                    // 移動元の「抜け殻」を一度だけ破棄する
                    m_archetype[ci].destructor.Destruct(src);
                }

                // ChunkManager に住所録を直してもらう
                if (onMoveCallback)
                {
                    onMoveCallback(currentID, this, write);
                }
            }

            ++read;
            ++write;
        }

        // 生き残った数が新しい使用スロット数になる
        m_entityCount = write;
        m_destroyedEntities.clear();
    }



    size_t Chunk::AddEntity(EntityID entity)
    {
        // 定員ぶんは Create で確保済みなので、空いているスロットに書くだけ

        // 満員は assert でしか止めない。Release では素通りするので、呼び出し側が定員を守る
        if (m_entityCount >= m_capacity)
        {
            assert(false && "Chunk is full!");
            return InvalidIndex;
        }

        size_t index = m_entityCount;

        m_ownerEntities[index] = entity;

        m_entityCount++;
        return index;
    }

} // namespace CCL::ECS
