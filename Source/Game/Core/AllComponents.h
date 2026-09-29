#pragma once
// ===========================================================================
// 概要: よく使うコンポーネントのヘッダーをまとめて include するためのヘッダー。
//
// 責務:
//   - タグ・物理・カメラ・描画・ゲームロジックの主なコンポーネントを1行で取り込めるようにする
//
// 備考: 今使っているのは BuildingScatterWindow だけ。コンパイル時間が延びるので、
//       普段は必要なヘッダーを個別に include する。
// ===========================================================================

// タグ
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Combat/EnemyTag.h"

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

// カメラ
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraBodyIntroOrbitComponent.h"

// 描画
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"

// ライト・ポストプロセス・パーティクル
#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Graphics/Components/Lighting/LightComponent.h"
#include "Engine/Graphics/Components/Lighting/ShadowMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/BloomConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"

// ゲームロジック
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"

#include "Engine/Transform/Components/PendingParentComponent.h"
