#pragma once
// ===========================================================================
// 概要: Jolt の衝突レイヤー（ObjectLayer / BroadPhaseLayer）の定義と、衝突フィルタ
//
// 責務:
//   - ObjectLayer の定数・個数・インスペクタ用の名前を X-Macro から生成する
//   - BroadPhaseLayer の定義
//   - 「どのレイヤーがどのバケツに入り、何と当たるか」を決めるフィルタ3種の宣言
//
// 備考: JoltPhysicsManager を include せずにレイヤーだけ使えるよう、ヘッダを分けている
//       レイヤーを増やすときは PHYSICS_OBJECT_LAYER_LIST に1行足す
//       どのバケツに入り何と当たるかは自動で決まらないので、PhysicsLayers.cpp のフィルタ3つと IsStaticWorldLayer も直す
// ===========================================================================

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

// ============================================================
// レイヤーの定義元（X-Macro）。(定数名, 値, 表示名)
// ============================================================
// 前提: プレイヤー vs ボスの1対1アクション
//   本体レイヤー: PLAYER / BOSS（+ 壁 NON_MOVING）
//   攻撃レイヤー: PLAYER_ATTACK(=ボスに命中) / BOSS_ATTACK(=プレイヤーに命中)
//     攻撃レイヤーで「誰に当てるか」を表現する。剣も空間判定(SpawnHitbox)も
//       この攻撃レイヤーを載せることで、味方誤爆せず相手にだけ当たる
//   INVISIBLE_WALL: 落下防止用の見えない壁。地形(NON_MOVING)と違い、
//     カメラのレイキャストにも飛行ドローン（BOSS_ATTACK 層）にも反応せず、地上キャラだけを止める
#define PHYSICS_OBJECT_LAYER_LIST(X)     \
    X(NON_MOVING, 0, "NON_MOVING")       \
    X(PLAYER, 1, "PLAYER")               \
    X(BOSS, 2, "BOSS")                   \
    X(PLAYER_ATTACK, 3, "PLAYER_ATTACK") \
    X(BOSS_ATTACK, 4, "BOSS_ATTACK")     \
    X(INVISIBLE_WALL, 5, "INVISIBLE_WALL")

// ============================================================
// ObjectLayer（物体の種類）。定数・個数・名前配列を X-Macro から生成する
// ============================================================
namespace PhysicsLayers
{
// (1) 定数（NON_MOVING = 0 ...）
#define X(name, value, str) static constexpr JPH::ObjectLayer name = value;
    PHYSICS_OBJECT_LAYER_LIST(X)
#undef X

// (2) 個数（自動カウント: 0 +1 +1 ... ）
#define X(name, value, str) +1
    static constexpr JPH::ObjectLayer NUM_LAYERS = 0 PHYSICS_OBJECT_LAYER_LIST(X);
#undef X

    // (3) Meta(インスペクタenum)用の表示名配列とカウント
    inline constexpr const char *LayerNames[] = {
#define X(name, value, str) str,
        PHYSICS_OBJECT_LAYER_LIST(X)
#undef X
    };
    static constexpr int LayerCount = static_cast<int>(NUM_LAYERS);

// X-Macro の値が 0,1,2,... と連番であることの検証
//   NUM_LAYERS は「行数」を数えているだけなので、飛び番(例: X(FOO, 9, "FOO"))を
//   書くと BPLayerInterfaceImpl::mObjectToBroadPhase[] の範囲外アクセスになる
//   JPH_ASSERT は Release/Development では消えるため、コンパイル時に止める
#define X(name, value, str) static_assert(name < NUM_LAYERS, #name " の値が NUM_LAYERS 以上です。PHYSICS_OBJECT_LAYER_LIST の値は 0 から連番にしてください");
    PHYSICS_OBJECT_LAYER_LIST(X)
#undef X

    // 動かない背景のレイヤーか（接触のイベントを出さなくてよいか）
    // 背景との接触は押し付けている間ずっと続き、OnContactPersisted が毎フレーム来るので、ここで弾かないとバッファが埋まる
    // レイヤーを増やしたら、PhysicsLayers.cpp のフィルタ3つとこの関数を見直す。漏れても、エラーなしに重くなるだけ
    inline constexpr bool IsStaticWorldLayer(JPH::ObjectLayer layer)
    {
        return layer == NON_MOVING || layer == INVISIBLE_WALL;
    }
}

// ============================================================
// BroadPhaseLayer（ブロードフェーズの区分け）。数が少なく設計判断そのものなので手書きで管理する
// ============================================================
namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1); // 動くもの全般（PLAYER/BOSS/攻撃レイヤーをまとめる）
    static constexpr JPH::uint            NUM_LAYERS(2);
}

// ============================================================
// 衝突フィルタ（実装は PhysicsLayers.cpp）
// ============================================================
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
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

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(
        JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(
        JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
};