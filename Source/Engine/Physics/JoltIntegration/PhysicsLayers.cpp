#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

using namespace JPH;

// ──────────────────────────────────────────────
// BPLayerInterfaceImpl
// ──────────────────────────────────────────────
BPLayerInterfaceImpl::BPLayerInterfaceImpl()
{
    // 「ObjectLayer の X」を持った奴を、どの「BroadPhase バケツ」に入れるかの対応表
    mObjectToBroadPhase[PhysicsLayers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[PhysicsLayers::MOVING]     = BroadPhaseLayers::MOVING;
    mObjectToBroadPhase[PhysicsLayers::ENEMY]      = BroadPhaseLayers::MOVING;
    mObjectToBroadPhase[PhysicsLayers::BULLET]     = BroadPhaseLayers::MOVING;
}

uint BPLayerInterfaceImpl::GetNumBroadPhaseLayers() const
{
    return BroadPhaseLayers::NUM_LAYERS;
}

BroadPhaseLayer BPLayerInterfaceImpl::GetBroadPhaseLayer(ObjectLayer inLayer) const
{
    JPH_ASSERT(inLayer < PhysicsLayers::NUM_LAYERS);
    return mObjectToBroadPhase[inLayer];
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char *BPLayerInterfaceImpl::GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const
{
    switch ((BroadPhaseLayer::Type)inLayer)
    {
    case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
        return "NON_MOVING";
    case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
        return "MOVING";
    default:
        JPH_ASSERT(false);
        return "INVALID";
    }
}
#endif

// ──────────────────────────────────────────────
// ObjectVsBroadPhaseLayerFilterImpl
//   BULLET は NON_MOVING(壁) のバケツとは照合しない（壁判定はDDAでやるため）。
// ──────────────────────────────────────────────
bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(
    ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const
{
    switch (inLayer1)
    {
    case PhysicsLayers::NON_MOVING:
        return inLayer2 == BroadPhaseLayers::MOVING;
    case PhysicsLayers::MOVING:
        return true; // キャラは全バケツと照合
    case PhysicsLayers::ENEMY:
        return true; // 敵は弾(MOVINGバケツ)等と照合する
    case PhysicsLayers::BULLET:
        return inLayer2 == BroadPhaseLayers::MOVING; // 弾は動くものバケツのみ（壁バケツは見ない）
    default:
        JPH_ASSERT(false);
        return false;
    }
}

// ──────────────────────────────────────────────
// ObjectLayerPairFilterImpl  ★命中ルールの確定版
//   BULLET × ENEMY  = true（命中）。
//   BULLET × BULLET = false（弾同士は当たらない＝O(n²)回避）。
//   BULLET × MOVING = false（弾はプレイヤーに当たらない）。
//   BULLET × NON_MOVING(壁) = false（壁はDDA）。
// ──────────────────────────────────────────────
bool ObjectLayerPairFilterImpl::ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const
{
    switch (inObject1)
    {
    case PhysicsLayers::NON_MOVING:
        // 壁は「キャラ(MOVING)」とだけ当たる（弾とは当たらない）
        return inObject2 == PhysicsLayers::MOVING;

    case PhysicsLayers::MOVING:
        // キャラ(プレイヤー)は 壁・他キャラ・敵 と当たる。弾とは当たらない。
        return inObject2 == PhysicsLayers::NON_MOVING || inObject2 == PhysicsLayers::MOVING || inObject2 == PhysicsLayers::ENEMY;

    case PhysicsLayers::ENEMY:
        // 敵は キャラ(プレイヤー)・弾 と当たる。
        return inObject2 == PhysicsLayers::MOVING
               //|| inObject2 == PhysicsLayers::ENEMY
               || inObject2 == PhysicsLayers::BULLET;

    case PhysicsLayers::BULLET:
        // 弾は「敵」とだけ当たる。弾同士・プレイヤー・壁とは当たらない。
        return inObject2 == PhysicsLayers::ENEMY;

    default:
        JPH_ASSERT(false);
        return false;
    }
}