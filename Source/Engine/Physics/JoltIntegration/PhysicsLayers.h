#pragma once
// ===================================================================================
// ファイル名: PhysicsLayers.h
// 役割: Jolt の衝突レイヤー定義（ObjectLayer / BroadPhaseLayer）と、
//       それらを解決する衝突フィルター群を集約するヘッダ。
//
// 【なぜ分離したか】
// - 以前は JoltPhysicsManager.h に同居していたが、System / Listener など
//   複数の場所から「レイヤー定数」を参照したい。
// - Manager 全体（PhysicsSystem/JobSystem など重い型）を include せずに、
//   レイヤーだけを軽く include できるようにして責務分離＋循環依存を回避する。
//
// 実装（フィルター各メソッドの中身）は PhysicsLayers.cpp にある。
// ===================================================================================

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

// ──────────────────────────────────────────────
// 1. 住人の種類 (ObjectLayer)
//    「この物体が何者か」を表すレイヤー。
// ──────────────────────────────────────────────
//namespace PhysicsLayers {
//    static constexpr JPH::ObjectLayer NON_MOVING = 0; // 壁・床（弾の壁判定はDDA。Joltの壁は基本使わない）
//    static constexpr JPH::ObjectLayer MOVING     = 1; // キャラ（プレイヤー・敵など押し合う動的アクター）
//    static constexpr JPH::ObjectLayer ENEMY      = 2; // 敵専用（弾の命中対象）
//    static constexpr JPH::ObjectLayer BULLET     = 3; // 弾専用（敵とだけ当たる・弾同士は当たらない）
//    static constexpr JPH::ObjectLayer NUM_LAYERS = 4;
//} // namespace PhysicsLayers

// ── レイヤーの唯一の定義元。ここに1行足すだけで定数・個数・名前配列が全部増える ──
#define PHYSICS_OBJECT_LAYER_LIST(X) \
    X(NON_MOVING, 0, "NON_MOVING")   \
    X(MOVING,     1, "MOVING")       \
    X(ENEMY,      2, "ENEMY")        \
    X(BULLET,     3, "BULLET")

namespace PhysicsLayers {
    // (1) 定数
    #define X(name, value, str) static constexpr JPH::ObjectLayer name = value;
    PHYSICS_OBJECT_LAYER_LIST(X)
    #undef X

    // (2) 個数（自動カウント）
    #define X(name, value, str) +1
    static constexpr JPH::ObjectLayer NUM_LAYERS = 0 PHYSICS_OBJECT_LAYER_LIST(X);
    #undef X

    // (3) Meta用の名前配列とカウント
    inline constexpr const char* LayerNames[] = {
        #define X(name, value, str) str,
        PHYSICS_OBJECT_LAYER_LIST(X)
        #undef X
    };
    static constexpr int LayerCount = static_cast<int>(NUM_LAYERS);
}

// ──────────────────────────────────────────────
// 2. 空間のバケツ (BroadPhaseLayer)
//    「この物体がどんな空間にいるか」を表すレイヤー。
// ──────────────────────────────────────────────
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);     // 動くもの全般（MOVING/ENEMY/BULLET をまとめる）
    static constexpr JPH::uint            NUM_LAYERS(2);
} // namespace BroadPhaseLayers

// ──────────────────────────────────────────────
// 3. 衝突フィルター実装クラス群
// ──────────────────────────────────────────────
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
  public:
    BPLayerInterfaceImpl();
    virtual JPH::uint            GetNumBroadPhaseLayers() const override;
    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif
  private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[PhysicsLayers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    virtual bool ShouldCollide(
        JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
  public:
    virtual bool ShouldCollide(
        JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
};

