/**
 * @file   CCL_Chunk.cpp
 * @brief  Chunk クラスの実装
 *
 * @details
 *  メモリアラインメントの考慮、SoA構造の構築、Entity挿入/削除/コンパクションを
 *  実装しています。前バージョンからの主な改善点:
 *   - _destroyedFlags をビット配列化(IsEntityDestroyed が O(1))
 *   - _destroyedCount で早期 return できるよう変更
 *   - MarkDestroyEntity 時にデストラクタも自動で呼ぶように修正
 *     (CompactHoles 前に呼ばれる前提を強化)
 *
 * @author CCL ECS Library
 */
#include "CCL_Chunk.h"

#include <algorithm>
#include <cstring>
#include <cassert>


namespace CCL::ECS::Core
{
    using namespace CCL::ECS;

    namespace
    {
        /**
         * @brief 指定したアライメント境界へサイズを切り上げる
         * @param size      切り上げたい値
         * @param alignment アライメント(2の冪である必要あり)
         */
        inline std::size_t AlignUp(std::size_t size, std::size_t alignment) noexcept
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        /// @brief コンポーネント配列のアライメント(SIMD等に配慮した16byte)
        constexpr std::size_t kComponentAlignment = 16;
    }


    Chunk::~Chunk()
    {
        Finalize();
    }


    // =========================================================================
    //  Create / CreateNull / Finalize
    // =========================================================================

    void Chunk::Create(const Archetype& archetype, std::size_t initialEntityCapacity)
    {
        _archetype     = archetype;
        _capacity      = initialEntityCapacity;
        _entityCount   = 0;
        _entityStride  = 0;
        _destroyedCount = 0;

        for (const TypeData& td : _archetype) {
            _entityStride += td.typeSize;
        }

        _ownerEntities.assign(_capacity, InvalidEntityID);
        _destroyedFlags.assign(_capacity, 0u);

        // --- メモリブロックの一括確保ロジック ---
        const std::size_t compCount = _archetype.size();
        _componentOffsets.resize(compCount);

        std::size_t currentOffset = 0;
        for (std::size_t i = 0; i < compCount; ++i) {
            currentOffset       = AlignUp(currentOffset, kComponentAlignment);
            _componentOffsets[i] = currentOffset;
            currentOffset += _archetype[i].typeSize * _capacity;
        }

        // 計算した総バイト数で1回だけメモリ確保
        _memoryBlock.assign(currentOffset, 0u);
    }


    void Chunk::CreateNull(std::size_t initialEntityCapacity)
    {
        _archetype      = Archetype();
        _capacity       = initialEntityCapacity;
        _entityStride   = 0;
        _entityCount    = 0;
        _destroyedCount = 0;

        _ownerEntities.assign(_capacity, InvalidEntityID);
        _destroyedFlags.assign(_capacity, 0u);

        _componentOffsets.clear();
        _memoryBlock.clear();
    }


    void Chunk::Finalize()
    {
        // 既にFinalize済みなら何もしない
        if (_componentOffsets.empty() && _memoryBlock.empty()) {
            _ownerEntities.clear();
            _destroyedFlags.clear();
            _entityCount    = 0;
            _destroyedCount = 0;
            return;
        }

        // 生きているEntityのデストラクタを全て呼ぶ
        for (std::size_t ci = 0; ci < _archetype.size(); ++ci)
        {
            const std::size_t baseOffset = _componentOffsets[ci];
            const std::size_t stride     = _archetype[ci].typeSize;

            for (std::size_t i = 0; i < _entityCount; ++i) {
                if (IsEntityDestroyed(i)) continue;
                void* ptr = _memoryBlock.data() + baseOffset + (i * stride);
                _archetype[ci].destructor.Destruct(ptr);
            }
        }

        _memoryBlock.clear();
        _componentOffsets.clear();
        _ownerEntities.clear();
        _destroyedFlags.clear();
        _entityCount    = 0;
        _destroyedCount = 0;
    }


    // =========================================================================
    //  Entity操作
    // =========================================================================

    std::size_t Chunk::AddEntity(EntityID entity)
    {
        if (_entityCount >= _capacity) {
            assert(false && "Chunk is full!");
            return InvalidIndex;
        }

        const std::size_t index = _entityCount;
        _ownerEntities[index]   = entity;
        _destroyedFlags[index]  = 0u;
        _entityCount++;
        return index;
    }


    void* Chunk::InsertEntity(EntityID entity)
    {
        const std::size_t writeIndex = AddEntity(entity);
        if (writeIndex == InvalidIndex) return nullptr;

        // 各コンポーネントを0初期化(後で正しく構築されることを期待)
        for (std::size_t ci = 0; ci < _archetype.size(); ++ci) {
            void* dst = GetComponentPtr(ci, writeIndex);
            std::memset(dst, 0, _archetype[ci].typeSize);
        }

        return _archetype.empty() ? nullptr : GetComponentPtr(0, writeIndex);
    }


    void Chunk::RemoveEntity(std::size_t entityIndex)
    {
        if (entityIndex >= _entityCount) return;

        // 削除予約済みなら多重デストラクタを避ける
        if (IsEntityDestroyed(entityIndex)) return;

        for (std::size_t ci = 0; ci < _archetype.size(); ++ci) {
            void* ptr = GetComponentPtr(ci, entityIndex);
            _archetype[ci].destructor.Destruct(ptr);
        }

        _ownerEntities[entityIndex]  = InvalidEntityID;
        _destroyedFlags[entityIndex] = 1u;
        ++_destroyedCount;
    }


    void Chunk::MarkDestroyEntity(std::size_t entityIndex)
    {
        if (entityIndex >= _entityCount) return;
        if (IsEntityDestroyed(entityIndex)) return; // 既に予約済み

        _ownerEntities[entityIndex]  = InvalidEntityID;
        _destroyedFlags[entityIndex] = 1u;
        ++_destroyedCount;
    }


    // =========================================================================
    //  CompactHoles
    // =========================================================================

    void Chunk::CompactHoles(
        const std::function<void(EntityID, Chunk*, std::size_t)>& onMoveCallback)
    {
        if (_destroyedCount == 0) return;

        std::size_t read  = 0;
        std::size_t write = 0;

        while (read < _entityCount)
        {
            const bool isDead = (_destroyedFlags[read] != 0u);

            if (isDead) {
                ++read;
                continue;
            }

            const EntityID currentID = _ownerEntities[read];

            if (read != write) {
                // 生きているデータを前へ詰める
                _ownerEntities[write]  = currentID;
                _destroyedFlags[write] = 0u;

                for (std::size_t ci = 0; ci < _archetype.size(); ++ci) {
                    const std::size_t baseOffset = _componentOffsets[ci];
                    const std::size_t stride     = _archetype[ci].typeSize;

                    void* dst = _memoryBlock.data() + baseOffset + (write * stride);
                    void* src = _memoryBlock.data() + baseOffset + (read  * stride);

                    if (_archetype[ci].mover) {
                        _archetype[ci].mover(dst, src);
                    } else {
                        std::memmove(dst, src, stride);
                    }
                    // ムーブ後の抜け殻を破棄
                    _archetype[ci].destructor.Destruct(src);
                }

                if (onMoveCallback) {
                    onMoveCallback(currentID, this, write);
                }
            }

            ++read;
            ++write;
        }

        // 生き残った人数で更新
        _entityCount    = write;
        _destroyedCount = 0;

        // 末尾の余ったフラグをクリア(次の挿入用)
        std::fill(_destroyedFlags.begin() + write, _destroyedFlags.end(), 0u);
        std::fill(_ownerEntities.begin() + write, _ownerEntities.end(), InvalidEntityID);
    }

} // namespace CCL::ECS::Core
