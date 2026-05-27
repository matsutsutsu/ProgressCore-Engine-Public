#pragma once

// タグ
#include "Game/Logics/Combat/DeadTag.h"

// 追加したコンポーネントをここで記述するオールヘッダー
#include "Engine/Transform/Components/TransformComponent.h"

#include "Engine/Core/Components/NameComponent.h"

// 物理
#include "Engine/Physics/Components/RigidBody/JoltRigidbodyComponent.h"
#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"

// キャラクターコントローラー
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"


#include "Game/Logics/Character/Enemy/EnemyComponent.h"

// カメラ

#include "Engine/Camera/Components/VirtualCameraComponents.h"

// 描画
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"

//#include "Game/GameLogics/Text/FloatingTextComponent.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
// シェーダー
#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Graphics/Components/Lighting/LightComponent.h"
#include "Engine/Graphics/Components/Lighting/ShadowMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/BloomConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"

#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Game/Utils/PooledParticleComponent.h"

// ゲームロジック


#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Character/PlayerCar/PlayerCarComponent.h"


#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Utils/TimerComponent.h"

#include "Engine/Transform/Components/PendingParentComponent.h"

