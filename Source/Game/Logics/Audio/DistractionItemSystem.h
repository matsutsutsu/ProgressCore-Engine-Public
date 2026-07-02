#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include <vector>
#include <mutex>

class DistractionItemSystem : public CCL::ECS::IfSystem<DistractionItemSystem> {
private:
    std::vector<JoltCollisionEvent> m_frameEvents;
    std::mutex                      m_mutex;


    void CheckAndTriggerSound(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& relVel);

public:
    DistractionItemSystem() : IfSystem("DistractionItemSystem") {}
    virtual ~DistractionItemSystem() = default;

    void Initialize() override;
    void Update(float dt) override;
};