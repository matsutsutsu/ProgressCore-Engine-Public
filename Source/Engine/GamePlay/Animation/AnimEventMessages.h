#pragma once
#include "ECS/Common/CCL_Common.h"

// ============================================================================
// アニメーションから各システムへ送信される「専用の手紙」群
// ============================================================================

// 1. 戦闘システム（CombatSystem）宛ての手紙
struct AnimEventHitBoxMessage {
    CCL::ECS::EntityID entity;
    bool isActive; // true なら判定ON、false なら判定OFF
};

// 2. 音響システム（AudioSystem）宛ての手紙
struct AnimEventSoundMessage {
    CCL::ECS::EntityID entity;
    // ※将来的に「どの音を鳴らすか」のIDなどをここに追加します
};

// 3. エフェクトシステム（ParticleSystem）宛ての手紙
struct AnimEventEffectMessage {
    CCL::ECS::EntityID entity;
    std::string effectPrefabPath; // 召喚すべきエフェクトの設計図
};