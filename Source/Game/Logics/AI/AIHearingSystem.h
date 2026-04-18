#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/AI/AIComponents.h"
#include "Engine/GamePlay/AI/Navigation/NavAgentComponent.h"
#include "Game/Logics/AI/Events/AISoundEvent.h"
#include <vector>
#include <mutex>

// 聴覚システム：音イベントを受け取り、AIの状態を調査(Investigate)へ切り替える
// ★規約準拠: Read/Write は1つずつ独立して記述する
class AIHearingSystem : public CCL::ECS::IfSystem<AIHearingSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<AIPerceptionComponent>,
    CCL::ECS::Write<AIMemoryComponent>,
    CCL::ECS::Write<AIStateComponent>,
    CCL::ECS::Write<NavAgentComponent>> {
private:
    std::vector<AISoundEvent> m_frameEvents;
    std::mutex m_eventMutex;

public:
    AIHearingSystem(const std::string& name = "AIHearingSystem") : IfSystem(name) {}
    virtual ~AIHearingSystem() = default;

    void Initialize() override;
    void Update(float dt) override;
};