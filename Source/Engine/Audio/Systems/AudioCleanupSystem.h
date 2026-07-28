// --- AudioCleanupSystem.h ---
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Audio/Components/AudioEmitterComponent.h"

/**
 * @brief 破棄される AudioEmitter の再生中サウンドを、実体が消える前に停止するシステム。
 *
 * @details
 *  AudioSystem は「生存しているエンティティ」しか走査しないため、BGM を鳴らしている
 *  エンティティが破棄されると StopEvent が呼ばれず、バックエンド側で音だけが鳴り続ける。
 *  そこで JoltCleanupSystem と同じ「DestroyTag を狙い撃つクリーンアップ」を用意し、
 *  破棄確定（ScrutinyAndApply）の前に確実に音を止める。
 *
 *  - Update(): 毎フレーム、DestroyTag が付いた AudioEmitter を止める（通常のゲーム内破棄）。
 *  - ExecuteCleanup(): シーン遷移などで手動一括破棄する経路からも呼べる静的版。
 */
class AudioCleanupSystem : public CCL::ECS::IfSystem<AudioCleanupSystem,
    CCL::ECS::Read<Tag::DestroyTag>, CCL::ECS::Write<AudioEmitterComponent>> {
public:
    AudioCleanupSystem() : IfSystem("AudioCleanupSystem") {}
    void Update(float dt) override;

    // 外部から強制的にクリーンアップを走らせる静的関数（シーン遷移時のteardown用）。
    static void ExecuteCleanup(CCL::ECS::World& world);
};
