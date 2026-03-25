#pragma once
#include <string>
#include <vector>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include "ECS/Common/CCL_Common.h"

// 1つのイベント通知 (エンジンが読み取る点)
struct AnimNotifyEvent {
    float time;              // 発火時間（秒）
    std::string eventName;   // "HitBox_Start", "HitBox_End", "Play_Sound" など
    std::string stringParam; // "Assets/Prefabs/Effects/SwordSlash.json" などのパスを書く欄

    template<class Archive>
    void serialize(Archive& archive) {
        archive(CEREAL_NVP(time), CEREAL_NVP(eventName), CEREAL_NVP(stringParam));
    }
};

// イベントを他システムへ伝えるための手紙
struct AnimNotifyMessage {
    CCL::ECS::EntityID entity;
    std::string eventName;
};


// 全エンティティで共有される「アニメーションの台本」
struct AnimSequence {
    std::string sequenceName;       // 例: "Goblin_Attack"
    std::string targetModelPath;    // 例: "Assets/Models/Goblin.gltf"
    std::string targetAnimName;     // 例: "Attack"
    float duration = 0.0f;          // アニメーションの総時間

    // ★必ず time の昇順でソートされていること！
    std::vector<AnimNotifyEvent> events;

    template<class Archive>
    void serialize(Archive& archive) {
        archive(CEREAL_NVP(sequenceName), CEREAL_NVP(targetModelPath),
            CEREAL_NVP(targetAnimName), CEREAL_NVP(duration), CEREAL_NVP(events));
    }
};
