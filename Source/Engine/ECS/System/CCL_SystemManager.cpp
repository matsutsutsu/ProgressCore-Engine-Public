/**
 * @file   CCL_SystemManager.cpp
 * @brief  SystemManagerの実装
 *
 * @details
 *  ResolveTargetChunks では、ArchetypeHelper::LhasR を使い、
 *  Chunkが各Systemのフィルタを包含しているかを線形チェックします。
 *  構造変更があるたびに対象を再収集することで、Spawn/Destroy後も正しい対象を維持します。
 *
 * @author CCL ECS Library
 */
#include "CCL_SystemManager.h"
#include "../Core/CCL_World.h"
#include "../Core/CCL_JobSystem.h"
#include "../Core/CCL_Chunk.h"
#include "../Core/CCL_ChunkManager.h"

#include <algorithm>


namespace CCL::ECS
{
    // =========================================================================
    //  ライフサイクル
    // =========================================================================

    SystemManager::SystemManager()  = default;
    SystemManager::~SystemManager() = default;


    // =========================================================================
    //  System登録時の初期化
    // =========================================================================

    void SystemManager::InjectAndInitialize(SystemBase* sys)
    {
        if (!sys) return;

        sys->SetWorld(_world);
        sys->SetJobSystem(_jobSystem);

        if (_world) {
            sys->SetStructureMutex(&_world->GetChunkManager().GetStructureMutex());
        }

        sys->Initialize();
    }


    // =========================================================================
    //  Update: 毎フレームの中核処理
    // =========================================================================

    void SystemManager::Update(float dt)
    {
        if (!_world) return;

        // 構造変更があったら対象Chunkを再収集
        if (_world->IsStructureDirty()) {
            ResolveTargetChunks();
            _world->ResetStructureDirty();
            _needsRebuild = true;
        }

        // 必要ならバッチ再構築
        if (_needsRebuild) {
            _scheduler.BuildGraph(_systems);
            _needsRebuild = false;
        }

        // 実行
        _scheduler.Execute(dt, _jobSystem);
    }


    // =========================================================================
    //  対象Chunk再収集
    // =========================================================================

    void SystemManager::ResolveTargetChunks()
    {
        if (!_world) return;

        const auto& chunks = _world->GetChunkManager().GetChunks();

        for (auto& sys : _systems)
        {
            sys->ClearTargetChunks();
            const Archetype& filter = sys->GetFilter();

            for (const auto& chunkPtr : chunks)
            {
                if (!chunkPtr) continue;

                // フィルタが空 → 全Chunkを対象に
                if (filter.empty()) {
                    sys->AddTargetChunk(chunkPtr.get());
                    continue;
                }

                // ChunkがSystemフィルタの全要素を持つか厳格判定
                if (ArchetypeHelper::LhasR(chunkPtr->GetArchetype(), filter)) {
                    sys->AddTargetChunk(chunkPtr.get());
                }
            }
        }
    }


    // =========================================================================
    //  クエリ
    // =========================================================================

    SystemBase* SystemManager::FindSystemByName(const std::string& name) const
    {
        for (const auto& s : _systems) {
            if (s->GetName() == name) return s.get();
        }
        return nullptr;
    }


    // =========================================================================
    //  GUI
    // =========================================================================

    void SystemManager::OnGui()
    {
        for (auto& s : _systems) {
            if (s->IsDebugVisible()) s->OnGui();
        }
    }

} // namespace CCL::ECS
