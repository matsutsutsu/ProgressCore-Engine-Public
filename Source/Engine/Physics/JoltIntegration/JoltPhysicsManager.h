#pragma once
// Joltの基本ヘッダー（必ず一番上に）
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

// 衝突レイヤー定義（ObjectLayer / BroadPhaseLayer / 各フィルター）
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

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
//
// 【レイヤー定義について】
// - レイヤー定数とフィルタークラスは PhysicsLayers.h に分離済み。
// ===================================================================================

// ──────────────────────────────────────────────
// JoltPhysicsManager クラス（心臓部）
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

    // ★ロックなし版。Step() 完了後で「誰も物理ボディに書き込まない」フェーズ限定で使う。
    //   （例: JoltPullSystem の読み取り専用同期）。それ以外では GetBodyInterface() を使うこと。
    JPH::BodyInterface &GetBodyInterfaceNoLock() const { return m_physicsSystem->GetBodyInterfaceNoLock(); }

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