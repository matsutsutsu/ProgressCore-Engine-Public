#pragma once
#include "ECS/Common/CCL_Common.h"
#include <json.hpp>

// 前方宣言 (AllComponents.h をインクルードしないため高速)
struct NameComponent;
struct TransformComponent;
struct RigidBodyComponent;
struct BoxColliderComponent;
struct SphereColliderComponent;
struct CapsuleColliderComponent;
struct CollisionConfigComponent;
struct ModelComponent;
struct StaticMeshComponent;
struct MaterialComponent;
struct PrimitiveComponent;
struct DirectionalLightComponent;
struct PointLightComponent;
struct SpotLightComponent;
struct FogComponent;
struct ShadowMapConfigComponent;
struct BloomConfigComponent;
struct ToneMapConfigComponent;
struct CameraComponent;
struct FreeCameraComponent;
struct PlayerComponent;
struct EnemyComponent;
struct GunComponent;
struct HealthComponent;
struct BoidsComponent;
struct GPUParticleComponent;
struct FloatingComponent;
struct PooledParticleComponent;
struct AnimatorComponent;

// ★Prefab.cppなどで get_to() を使うために必要な前方宣言
struct ParticleSystemConfig;

// ★重要: nlohmann::json が見つけられるように、関数の「宣言」だけをここに置く
// 実装は .cpp にあるため、これをインクルードする側は詳細を知らなくて済む
void to_json(nlohmann::json &j, const ParticleSystemConfig &c);
void from_json(const nlohmann::json &j, ParticleSystemConfig &c);

namespace ComponentSerializers {
    // 全コンポーネントのシリアライザを一括登録する関数
    void RegisterAll();

    // 各コンポーネントのシリアライズ関数宣言
    // (必要であれば外部から呼べるように公開していますが、基本はRegisterAll内で使われます)
    void Serialize(nlohmann::json &j, const NameComponent &c);
    void Deserialize(const nlohmann::json &j, NameComponent &c);

    void Serialize(nlohmann::json &j, const TransformComponent &c);
    void Deserialize(const nlohmann::json &j, TransformComponent &c);

    // ... 他のコンポーネントも同様のパターンですが、
    // ここでは冗長になるため省略し、RegisterAllだけ公開して
    // 実装ファイル内でオーバーロード解決させてもOKです。
    // 今回はシンプルに「RegisterAll」さえあれば機能します。
} // namespace ComponentSerializers