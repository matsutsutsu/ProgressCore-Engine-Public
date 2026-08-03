#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

using namespace JPH;

// ──────────────────────────────────────────────
// BPLayerInterfaceImpl
// ──────────────────────────────────────────────
BPLayerInterfaceImpl::BPLayerInterfaceImpl()
{
    // 「ObjectLayer の X」を持った奴を、どの「BroadPhase バケツ」に入れるかの対応表
    mObjectToBroadPhase[PhysicsLayers::NON_MOVING]    = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[PhysicsLayers::PLAYER]        = BroadPhaseLayers::MOVING;
    mObjectToBroadPhase[PhysicsLayers::BOSS]          = BroadPhaseLayers::MOVING; // ボスも動くキャラ扱い
    mObjectToBroadPhase[PhysicsLayers::PLAYER_ATTACK] = BroadPhaseLayers::MOVING; // 攻撃判定も動くもの側
    mObjectToBroadPhase[PhysicsLayers::BOSS_ATTACK]   = BroadPhaseLayers::MOVING;
    mObjectToBroadPhase[PhysicsLayers::INVISIBLE_WALL] = BroadPhaseLayers::NON_MOVING; // 見えない壁も静止物バケツ
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
//   攻撃レイヤーは NON_MOVING(壁) バケツとは照合しない（壁は攻撃を止めない）。
// ──────────────────────────────────────────────
bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(
    ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const
{
    switch (inLayer1)
    {
    case PhysicsLayers::NON_MOVING:
        return inLayer2 == BroadPhaseLayers::MOVING;
    case PhysicsLayers::PLAYER:
        return true; // プレイヤーは全バケツと照合（壁・ボス）
    case PhysicsLayers::BOSS:
        return true; // ボスは全バケツと照合（地面に立ち、プレイヤーと当たる）
    case PhysicsLayers::PLAYER_ATTACK:
        return inLayer2 == BroadPhaseLayers::MOVING; // 攻撃はキャラ(MOVINGバケツ)のみ
    case PhysicsLayers::BOSS_ATTACK:
        return inLayer2 == BroadPhaseLayers::MOVING;
    case PhysicsLayers::INVISIBLE_WALL:
        return inLayer2 == BroadPhaseLayers::MOVING; // 止める相手はキャラ(MOVINGバケツ)だけ
    default:
        JPH_ASSERT(false);
        return false;
    }
}

// ──────────────────────────────────────────────
// ObjectLayerPairFilterImpl  命中ルールの確定版（プレイヤー vs ボス）
//   本体同士: PLAYER × BOSS = true（押し合う）。壁は本体だけ止める。
//   攻撃レイヤーは「相手の本体」にだけ当たる:
//     PLAYER_ATTACK × BOSS   = true（プレイヤーの攻撃はボスに命中）
//     BOSS_ATTACK   × PLAYER = true（ボスの攻撃はプレイヤーに命中）
//   攻撃 × 攻撃、攻撃 × 自分、攻撃 × 壁 は false（誤爆・壁ヒットしない）。
//   ※対称に呼ばれても成立するよう両方向で一致させている。
// ──────────────────────────────────────────────
bool ObjectLayerPairFilterImpl::ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const
{
    switch (inObject1)
    {
    case PhysicsLayers::NON_MOVING:
        // 壁/地面は本体(PLAYER/BOSS)だけ止める。攻撃判定はすり抜ける。
        return inObject2 == PhysicsLayers::PLAYER || inObject2 == PhysicsLayers::BOSS;

    case PhysicsLayers::PLAYER:
        // プレイヤー本体は 壁・見えない壁・ボス本体・ボスの攻撃 と当たる。
        return inObject2 == PhysicsLayers::NON_MOVING
               || inObject2 == PhysicsLayers::INVISIBLE_WALL
               || inObject2 == PhysicsLayers::BOSS
               || inObject2 == PhysicsLayers::BOSS_ATTACK;

    case PhysicsLayers::BOSS:
        // ボス本体は 壁・見えない壁・プレイヤー本体・プレイヤーの攻撃 と当たる。
        return inObject2 == PhysicsLayers::NON_MOVING
               || inObject2 == PhysicsLayers::INVISIBLE_WALL
               || inObject2 == PhysicsLayers::PLAYER
               || inObject2 == PhysicsLayers::PLAYER_ATTACK;

    case PhysicsLayers::PLAYER_ATTACK:
        // プレイヤーの攻撃は「ボス本体」にだけ命中。
        return inObject2 == PhysicsLayers::BOSS;

    case PhysicsLayers::BOSS_ATTACK:
        // ボスの攻撃は「プレイヤー本体」にだけ命中。
        return inObject2 == PhysicsLayers::PLAYER;

    case PhysicsLayers::INVISIBLE_WALL:
        // 落下防止の見えない壁。地上を歩く本体(PLAYER/BOSS)だけを止める。
        //   ・攻撃判定       → 素通り（NON_MOVING と同じ扱い）
        //   ・飛行ドローン   → BOSS_ATTACK 層なので素通り
        //   ・カメラのレイ   → RayCastStatic が ObjectLayer==NON_MOVING を狙い撃ちする
        //                      ため、別レイヤーの本壁は自動的に無視される
        return inObject2 == PhysicsLayers::PLAYER || inObject2 == PhysicsLayers::BOSS;

    default:
        JPH_ASSERT(false);
        return false;
    }
}