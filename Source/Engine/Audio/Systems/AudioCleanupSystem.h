#pragma once
// ===========================================================================
// 概要: このフレームで破棄される Entity の、鳴っている音を止める System（L07）
//
// 備考: AudioSystem は生きている Entity しか回らないので、破棄された Entity の音は止まらずに鳴り続ける
//       そのため JoltCleanupSystem と同じく、破棄が確定する前（ScrutinyAndApply の前）に止める
//       消える Entity は HierarchyCleanupSystem::CollectDoomedEntities で求める
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Audio/Components/AudioEmitterComponent.h"

class AudioCleanupSystem : public CCL::ECS::IfSystem<
    AudioCleanupSystem,
    CCL::ECS::Read<Tag::DestroyTag>,
    CCL::ECS::Write<AudioEmitterComponent>>
{
public:
    AudioCleanupSystem() : IfSystem("AudioCleanupSystem") {}
    void Update(float dt) override;

    // Update を待たずに後始末を走らせる。シーン遷移時に SceneManager が呼ぶ
    static void ExecuteCleanup(CCL::ECS::World &world);
};
