#pragma once
// Joltの基本ヘッダー（必ず一番上に）
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

// 衝突レイヤー関連
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include "JoltContactListener.h"

#include <memory>

// ===================================================================================
// ファイル名: JoltPhysicsManager.h
// 役割: Jolt
// Physicsのライフサイクルとメモリ・スレッドを管理する「神（God）」クラス。
//
// 【アーキテクチャ仕様】
// -
// ECSのComponentやSystemには直接依存せず、純粋な物理空間のサンドボックスを提供する。
// - 内部で TempAllocator (一時メモリ) と JobSystem (並列計算スレッド)
// を保持する。
//
// 【使い方・ルール】
// - ゲーム開始時に Initialize() を呼び、毎フレーム Step() を呼ぶ。
// - System側からは _world->GetResource<JoltPhysicsManager>() 経由でアクセスし、
//   GetBodyInterface() を通じて剛体を操作する。直接 JPH::PhysicsSystem
//   を弄らないこと。
// ===================================================================================


// ──────────────────────────────────────────────
// 1. レイヤー定義
// ──────────────────────────────────────────────

// --- 1. 住人の種類 (ObjectLayer) ---
// ObjectLayer は「この物体が何者か」を定義するためのレイヤーです。
namespace PhysicsLayers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0; // 壁・床（※本作では弾の壁判定はDDA。Joltの壁は基本使わない）
    static constexpr JPH::ObjectLayer MOVING     = 1; // キャラ（プレイヤー・敵など押し合う動的アクター）
    static constexpr JPH::ObjectLayer ENEMY      = 2; // 敵専用（弾の命中対象）
    static constexpr JPH::ObjectLayer BULLET     = 3; // 弾専用（敵とだけ当たる・弾同士は当たらない）
    static constexpr JPH::ObjectLayer NUM_LAYERS = 4;
} // namespace PhysicsLayers

// --- 2. 空間のバケツ (BroadPhaseLayer) ---
// BroadPhaseLayer は「この物体がどんな空間にいるか」を定義するためのレイヤーです。
// 普通になんの性質を持っているかのレイヤーの区分
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);     // 動くもの全般（MOVING/ENEMY/BULLET をまとめる）
    static constexpr JPH::uint            NUM_LAYERS(2);
} // namespace BroadPhaseLayers

// ──────────────────────────────────────────────
// 2. 衝突フィルター実装クラス（内部使用）
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

// ──────────────────────────────────────────────
// 3. JoltPhysicsManager クラス（心臓部）
// ──────────────────────────────────────────────
class JoltPhysicsManager {
  public:
    JoltPhysicsManager() = default;
    ~JoltPhysicsManager();

    // コピー・ムーブを禁止（世界に1つだけの存在にするため）
    JoltPhysicsManager(const JoltPhysicsManager &)            = delete;
    JoltPhysicsManager &operator=(const JoltPhysicsManager &) = delete;

    void Initialize();
    void Step(float deltaTime);

    // ECSシステムから物理空間にアクセスするためのゲッター
    JPH::PhysicsSystem *GetPhysicsSystem() const { return m_physicsSystem.get(); }
    JPH::BodyInterface &GetBodyInterface() const { return m_physicsSystem->GetBodyInterface(); }
    // CharacterVirtual の更新などに必要な一時アロケータを取得する
    JPH::TempAllocator* GetTempAllocator() const { return m_tempAllocator.get(); }

    // ECSシステムからリスナー（ポスト）にアクセスするためのゲッター
    JoltContactListener* GetContactListener() const { return m_contactListener.get(); }

  private:
    // Joltのコアシステム（破棄順序を制御するため unique_ptr で管理）
    std::unique_ptr<JPH::PhysicsSystem>       m_physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl>   m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
    // リスナーのインスタンスを保持
    std::unique_ptr<JoltContactListener> m_contactListener;


    // フィルターのインスタンス
    BPLayerInterfaceImpl              m_bpLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadphaseFilter;
    ObjectLayerPairFilterImpl         m_objectVsObjectFilter;


    bool m_isInitialized = false;
};