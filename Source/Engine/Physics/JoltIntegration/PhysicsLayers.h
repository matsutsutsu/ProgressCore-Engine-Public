#pragma once
// ===================================================================================
// ファイル名: PhysicsLayers.h
// 役割: Jolt の衝突レイヤー定義（ObjectLayer / BroadPhaseLayer）と、
//       それらを解決する衝突フィルター群を集約するヘッダ。
//
// 【なぜ分離したか】
// - System / Listener / Component など複数箇所から「レイヤー定数」を参照したい。
// - Manager 全体（PhysicsSystem/JobSystem など重い型）を include せずに、
//   レイヤーだけを軽く include できるようにして責務分離＋循環依存を回避する。
//
// 【レイヤーを増やすとき】
// - 下の PHYSICS_OBJECT_LAYER_LIST に1行足すだけで、
//   「定数 / 個数(NUM_LAYERS) / Meta用の名前配列(LayerNames)」が全部自動で増える。
// - ただし「どのバケツに入るか」「何と当たるか」は policy なので、
//   PhysicsLayers.cpp の3フィルタを必ず手で更新すること（自動化不可）。
//
// 実装（フィルター各メソッドの中身）は PhysicsLayers.cpp にある。
// ===================================================================================

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

// ──────────────────────────────────────────────
// レイヤーの唯一の定義元（X-Macro）
//    (定数名, 値, 表示名)  ← ここに1行足すだけで全部が増える
// ──────────────────────────────────────────────
// 前提: プレイヤー vs ボスの1対1アクション。
//   本体レイヤー: PLAYER / BOSS（+ 壁 NON_MOVING）。
//   攻撃レイヤー: PLAYER_ATTACK(=ボスに命中) / BOSS_ATTACK(=プレイヤーに命中)。
//     攻撃レイヤーで「誰に当てるか」を表現する。剣も空間判定(SpawnHitbox)も
//       この攻撃レイヤーを載せることで、味方誤爆せず相手にだけ当たる。
//   INVISIBLE_WALL: 落下防止用の「見えない壁」。地形(NON_MOVING)と違い、
//     カメラのレイキャストにも飛行ドローンにも反応せず、地上キャラだけを止める。
#define PHYSICS_OBJECT_LAYER_LIST(X)         \
    X(NON_MOVING,    0, "NON_MOVING")        \
    X(PLAYER,        1, "PLAYER")            \
    X(BOSS,          2, "BOSS")              \
    X(PLAYER_ATTACK, 3, "PLAYER_ATTACK")     \
    X(BOSS_ATTACK,   4, "BOSS_ATTACK")       \
    X(INVISIBLE_WALL,5, "INVISIBLE_WALL")

// ──────────────────────────────────────────────
// 1. 住人の種類 (ObjectLayer)  ※定数・個数・名前配列を X-Macro から自動生成
// ──────────────────────────────────────────────
namespace PhysicsLayers {
    // (1) 定数（NON_MOVING = 0 ...）
    #define X(name, value, str) static constexpr JPH::ObjectLayer name = value;
    PHYSICS_OBJECT_LAYER_LIST(X)
    #undef X

    // (2) 個数（自動カウント: 0 +1 +1 ... ）
    #define X(name, value, str) +1
    static constexpr JPH::ObjectLayer NUM_LAYERS = 0 PHYSICS_OBJECT_LAYER_LIST(X);
    #undef X

    // (3) Meta(インスペクタenum)用の表示名配列とカウント
    inline constexpr const char* LayerNames[] = {
        #define X(name, value, str) str,
        PHYSICS_OBJECT_LAYER_LIST(X)
        #undef X
    };
    static constexpr int LayerCount = static_cast<int>(NUM_LAYERS);
} // namespace PhysicsLayers

// ──────────────────────────────────────────────
// 2. 空間のバケツ (BroadPhaseLayer)
//    「どんな空間にいるか」。数が少なく policy 性が強いので手書きで管理する。
// ──────────────────────────────────────────────
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);     // 動くもの全般（MOVING/ENEMY/BULLET をまとめる）
    static constexpr JPH::uint            NUM_LAYERS(2);
} // namespace BroadPhaseLayers

// ──────────────────────────────────────────────
// 3. 衝突フィルター実装クラス群（実装は PhysicsLayers.cpp）
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