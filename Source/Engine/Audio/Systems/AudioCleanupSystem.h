#pragma once
// ===========================================================================
// 概要: このフレームで破棄される Entity の再生中の音を止める System（L07）。
//
// 責務:
//   - 破棄される Entity（DestroyTag の起点 + 子孫）の AudioEmitter の音を止める
//
// 備考:
//   - AudioSystem は生きている Entity しか走査しないので、音を鳴らしている Entity が
//     破棄されると StopEvent が呼ばれず、バックエンドで音だけが鳴り続ける。
//     そのため JoltCleanupSystem と同じく、破棄の確定（ScrutinyAndApply）より前に止める。
//   - 「誰が消えるか」は HierarchyCleanupSystem::CollectDoomedEntities に一本化している。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Audio/Components/AudioEmitterComponent.h"

class AudioCleanupSystem : public CCL::ECS::IfSystem<AudioCleanupSystem,
                                                     CCL::ECS::Read<Tag::DestroyTag>, CCL::ECS::Write<AudioEmitterComponent>>
{
public:
    AudioCleanupSystem() : IfSystem("AudioCleanupSystem") {}
    void Update(float dt) override;

    // Update を待たずに後始末を走らせる。シーン遷移時に SceneManager が呼ぶ
    static void ExecuteCleanup(CCL::ECS::World &world);
};
