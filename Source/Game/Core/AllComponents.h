#pragma once

// タグ
#include "Game/Logics/Combat/DeadTag.h"

// 追加したコンポーネントをここで記述するオールヘッダー
#include "Engine/GamePlay/Transform/TransformComponent.h"

#include "Engine/GamePlay/Core/NameComponent.h"

// 物理
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/BoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/CapsuleColliderComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Physics/Collision/CollisionConfigComponent.h"


// カメラ

#include "Engine/GamePlay/Camera/VirtualCameraComponents.h"

// 描画
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Graphics/Core/PrimitiveComponent.h"
#include "Engine/GamePlay/Graphics/Core/StaticMeshComponent.h"
#include "Engine/GamePlay/Graphics/Core/MaterialComponent.h"

//#include "Game/GameLogics/Text/FloatingTextComponent.h"

#include "Engine/GamePlay/Animation/AnimatorComponent.h"
// シェーダー
#include "Engine/GamePlay/Graphics/Lighting/FogComponent.h"
#include "Engine/GamePlay/Graphics/Lighting/LightComponent.h"
#include "Engine/GamePlay/Graphics/Lighting/ShadowMapConfigComponent.h"
#include "Engine/GamePlay/Graphics/PostProcess/BloomConfigComponent.h"
#include "Engine/GamePlay/Graphics/PostProcess/ToneMapConfigComponent.h"

#include "Engine/GamePlay/Graphics/Particle/GPUParticleComponent.h"
#include "Game/Utils/PooledParticleComponent.h"

// ゲームロジック
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"
#include "Game/Logics/Combat/Weapon/Core/GunComponent.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h"
#include "Game/Logics/Character/Enemy/FloatingComponent.h"

#include "Game/Logics/Character/Enemy/BossComponent.h"

#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/GamePlay/Utils/TimerComponent.h"

#include "Game/Logics/World/Triggers/TriggerComponent.h"

#include "Engine/Scripting/LuaScriptComponent.h"

#include "Engine/GamePlay/Transform/PendingParentComponent.h"

#include "Game/Logics/Character/Player/PlayerDeathSequenceComponent.h"