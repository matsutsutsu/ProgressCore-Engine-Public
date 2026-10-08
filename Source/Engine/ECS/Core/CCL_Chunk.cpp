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
        // ここが一番の勘所:
        //   各コンポーネント列の幅は「今いる数」ではなく【定員】で決まる。
        //   定員682体なら、8体しか居なくても Position 列は682体分の幅を持つ。
        //   そのため Velocity 列は 8192 バイトも先から始まる（Tools/ECSPlayground の実験1で見る値）。
        //   この設計のおかげで、以降 Entity を何体足してもメモリ確保は二度と起きない。

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
        const size_t ALIGNMENT     = 16; // SIMD 命令等を見据えた16バイトアライメント

        for (size_t i = 0; i < compCount; ++i)
        {
            // 1. 列の開始位置を16の倍数に切り上げる（CPU が読みやすい番地にする）
            currentOffset = AlignUp(currentOffset, ALIGNMENT);

            // 2. この列の開始位置を記録する
            m_componentOffsets[i] = currentOffset;

            // 3. 列の幅（1個のサイズ × 定員）だけ進める
            //    例: Position(12byte) が定員100なら、1200バイト先へ
            currentOffset += m_archetype[i].typeSize * m_capacity;
        }

        // 4. 必要な総バイト数で、メモリを1回だけ確保する
        m_memoryBlock.resize(currentOffset);

        m_entityCount = 0;
        m_destroyedEntities.clear();
    }

    void Chunk::Finalize()
    {
        // m_componentOffsets が空 ＝「まだ Create していない」か「もう Finalize 済み」。
        // 二重解放を防ぐため何もしない
        if (m_componentOffsets.empty()) return;

        // 1. 生きている要素だけに、型ごとのデストラクタを呼ぶ。
        //    m_memoryBlock は生メモリなので、型が分からないと正しく破棄できない。
        //    そのために TypeData がデストラクタの関数ポインタを持っている（型消去）
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
        // 破棄が2段階になっている理由:
        //   System が ForEach でループしている最中にスロットが前に詰まると、
        //   ループが握っているポインタやインデックスが壊れる。
        //   そこで「印を付けるだけ」に留め、実際に詰めるのは
        //   フレーム境界の CompactAllChunks → CompactHoles でまとめて行う。

        if (entityIndex >= m_entityCount) return;

        m_ownerEntities[entityIndex] = InvalidEntityID;
        m_destroyedEntities.push_back(entityIndex);
    }

    void Chunk::CompactHoles(const std::function<void(EntityID, Chunk *, size_t)> &onMoveCallback)
    {
        // アルゴリズム（2本指の古典パターン）:
        //   read  = 走査位置（前から順に全部見ていく指）
        //   write = 詰め先位置（生きている要素を置く指）
        //
        //   空きスロット           → read だけ進める（write は据え置き＝穴を飛ばす）
        //   生きていて前に穴が有る → read の中身を write へ移し、両方進める
        //   生きていて前に穴が無い → 何もせず両方進める（read == write なので移動不要）
        //
        // 例: スロット [A, ×, B, ×, C]（× は削除予約）
        //   read=0 A  : 穴なし → そのまま            write=1  [A, ×, B, ×, C]
        //   read=1 ×  : 飛ばす                       write=1
        //   read=2 B  : 穴あり → B を 1 へ移す       write=2  [A, B, -, ×, C]
        //   read=3 ×  : 飛ばす                       write=2
        //   read=4 C  : 穴あり → C を 2 へ移す       write=3  [A, B, C, -, -]
        //   最後に m_entityCount = write = 3。後ろの2スロットは次の AddEntity で上書きされる。
        //
        // 移動のしかた:
        //   型の mover（ムーブ構築）があればそれを使い、無ければ memmove で生コピー。
        //   ムーブした後の移動元は「抜け殻」になるので、デストラクタを呼んで始末する
        //   （std::string や shared_ptr をムーブした後を想像すると分かりやすい）。
        //
        // Tools/ECSPlayground の実験3で「最後尾の Entity が空いた穴へ移動する」のがこの関数の結果。

        if (m_destroyedEntities.empty()) return;

        size_t read  = 0;
        size_t write = 0;

        while (read < m_entityCount)
        {
            EntityID currentID = m_ownerEntities[read];

            bool isDead = (currentID == InvalidEntityID);

            if (isDead)
            {
                // デストラクタは呼び出し元（ApplyDestroyOp など）で呼び終えているので、
                // ここでは読むだけ進めて穴を飛ばせばよい
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

                // EntityID → 位置の対応表を ChunkManager に更新してもらう
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
        // Create() の時点で定員分を確保済みなので、ここは「空いているスロットに名前を書く」
        // だけで終わる。ECS が大量の Entity 生成に強いのはこの構造のおかげ。

        // 満員時は assert のみ。Release では assert が消えるので、
        // 呼び出し側が定員を守る前提になっている（既知の弱点）
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
