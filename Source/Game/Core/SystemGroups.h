#pragma once

// ECS Core
#include "ECS/System/CCL_SystemManager.h"
#include "Game/Core/SystemPriority.h"
#include "Engine/Graphics/Core/Graphics.h"

// --- Logic: Physics & Core ---
#include "Engine/GamePlay/Transform/TransformUpdateSystem.h"
#include "Engine/GamePlay/Physics/RigidBody/GridUpdateSystem.h"
#include "Engine/GamePlay/Physics/Collision/CollisionSystem.h"
#include "Engine/GamePlay/Physics/RigidBody/PhysicsIntegrationSystem.h"
#include "Engine/GamePlay/Physics/Collision/GroundCheckSystem.h"
#include "Engine/GamePlay/Physics/Collision/WallCollisionSystem.h"

// --- Logic: Gameplay (Player, Enemy, Weapon) ---
#include "Game/Logics/Character/Player/PlayerInputSystem.h"
#include "Game/Logics/Character/Player/PlayerMoveSystem.h"
#include "Game/Logics/Character/Player/PlayerShootingSystem.h"
#include "Game/Logics/Character/Enemy/EnemyBehaviorSystem.h"

#include "Game/Logics/Combat/Weapon/BaseSystems/BulletUpdateSystem.h"
#include "Game/Logics/Combat/Weapon/BaseSystems/BulletCollisionSystem.h"
#include "Game/Logics/Combat/Weapon/BaseSystems/BulletRaycastSystem.h"

#include "Game/Logics/Character/Enemy/EnemySpawnSystem.h"
#include "Game/Logics/Character/Enemy/FloatingSystem.h"

#include "Game/Logics/Character/Enemy/BossBehaviorSystem.h"

// --- Object ---
#include "Game/Logics/World/Triggers/TriggerSystem.h"
#include "Game/Logics/World/Systems/StruckRescueSystem.h"
#include "Game/Logics/Combat/Loot/CoinSystem.h"

// --- UI ---

// Utils
#include "Engine/GamePlay/Utils/TimerSystem.h"


// --- Render ---
#include "Engine/GamePlay/Camera/CameraBrainSystem.h"
#include "Engine/GamePlay/Camera/VisualCameraSystem.h"


#include "Engine/GamePlay/Graphics/Core/DissolveSystem.h"
#include "Engine/GamePlay/Graphics/Core/ModelRenderSystem.h"
#include "Engine/GamePlay/Graphics/Core/PrimitiveRenderSystem.h"
#include "Engine/GamePlay/Graphics/Lighting/ShadowMapRenderSystem.h"
#include "Engine/GamePlay/Graphics/Lighting/FogSystem.h"
#include "Engine/GamePlay/Graphics/Lighting/LightSystem.h"
#include "Engine/GamePlay/Graphics/PostProcess/PostProcessSystem.h"
#include "Game/Logics/World/Systems/MapDebugSystem.h" // 必要ならコメントアウト解除

#include "Engine/GamePlay/Animation/AnimationSystem.h"

#include "Engine/Scripting/LuaScriptSystem.h"

#include "Engine/GamePlay/Transform/HierarchyCleanupSystem.h"

#include "Engine/GamePlay/Graphics/Particle/GPUParticleUpdateSystem.h"
#include "Engine/GamePlay/Graphics/Particle/GPUParticleRenderSystem.h"

namespace SystemGroups {
    using namespace CCL::ECS;
    using namespace Priority; // ★名前空間を省略できるようにする

    // ---------------------------------------------------------
    // パーティクルシステム
    // ---------------------------------------------------------
    inline std::shared_ptr<GPUParticleRenderSystem> RegisterParticleSystems(
        SystemManager &manager)
    {
        // ParticleUpdate更新　はマクロの方で登録　（Priority::RenderStage）
  
        // 描画 (Manual)
        auto renderer = std::make_shared<GPUParticleRenderSystem>();
        manager.RegisterManualSystem(renderer.get());

        return renderer;
    }
} // namespace SystemGroups