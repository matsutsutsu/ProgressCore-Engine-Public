#pragma once
// ===========================================================================
// 概要: Jolt Physics の初期化・毎ステップの更新・終了処理を受け持つクラス。
//
// 責務:
//   - PhysicsSystem・TempAllocator・ジョブシステム・接触リスナーの生成と破棄
//   - 物理の時間を進める（Step）
//   - System へ BodyInterface などの入口を公開する
//
// 備考: ECS の Component や System には依存しない。System からは World のリソース
//       JoltPhysicsManager* を引き、GetBodyInterface() 経由で剛体を操作する。
//       JPH::PhysicsSystem を直接いじらないこと。
//       レイヤー定数とフィルタは PhysicsLayers.h にある。
// ===========================================================================
// Jolt の基本ヘッダ（他の Jolt ヘッダより先に）
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

// 衝突レイヤー定義（ObjectLayer / BroadPhaseLayer / 各フィルター）
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

#include "JoltContactListener.h"
#include "JoltJobSystemAdapter.h"

#include <memory>

// ECS側の共有スレッドプール（前方宣言。実体は CCL_JobSystem.h）
namespace CCL::ECS
{
    class JobSystem;
}

class JoltPhysicsManager
{
public:
    JoltPhysicsManager() = default;
    ~JoltPhysicsManager();

    // コピー禁止（Jolt のグローバル状態 Factory::sInstance を1つだけ持つため）
    JoltPhysicsManager(const JoltPhysicsManager &)            = delete;
    JoltPhysicsManager &operator=(const JoltPhysicsManager &) = delete;

    /**
     * @brief 物理世界を初期化する。
     *
     * @param inSharedPool
     *   ECS と共有するスレッドプール。渡すと物理ジョブもこのプールで走る
     *   （JoltJobSystemAdapter 経由。スレッドプールが1本に統合され、
     *     物理ジョブが Tracy にジョブ名付きゾーンとして出る）。
     *   nullptr を渡すと Jolt 純正の JobSystemThreadPool を自前で立てる。
     *   切り分け・比較用に残してある（Framework は共有プールを渡している）。
     */
    void Initialize(CCL::ECS::JobSystem *inSharedPool = nullptr);

    // 物理の時間を deltaTime 秒進める（1/30 秒で頭打ち）。JoltStepSystem から呼ぶ
    void Step(float deltaTime);

    JPH::PhysicsSystem *GetPhysicsSystem() const { return m_physicsSystem.get(); }
    JPH::BodyInterface &GetBodyInterface() const { return m_physicsSystem->GetBodyInterface(); }

    // ロックなし版。Step() 完了後で「誰も物理ボディに書き込まない」フェーズ限定で使う
    //   （例: JoltPullSystem の読み取り専用同期）。それ以外では GetBodyInterface() を使う。
    JPH::BodyInterface &GetBodyInterfaceNoLock() const { return m_physicsSystem->GetBodyInterfaceNoLock(); }

    // CharacterVirtual の更新などに必要な一時アロケータを取得する
    JPH::TempAllocator *GetTempAllocator() const { return m_tempAllocator.get(); }

    // 接触イベントのバッファ（剛体・キャラクター共通）
    JoltContactListener *GetContactListener() const { return m_contactListener.get(); }

private:
    // Joltのコアシステム（破棄順序を制御するため unique_ptr で管理）
    std::unique_ptr<JPH::PhysicsSystem>     m_physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;

    // --- ジョブシステムは2択。実体を持つのはどちらか一方だけ ---
    std::unique_ptr<JoltJobSystemAdapter>     m_adapterJobSystem; // 共有プールモード: ECS のプールに委譲するアダプタ
    std::unique_ptr<JPH::JobSystemThreadPool> m_nativeJobSystem;  // 単独モード: Jolt 純正のスレッドプール
    JPH::JobSystem                           *m_activeJobSystem = nullptr; // PhysicsSystem::Update へ渡す方（上のどちらか。所有しない）

    std::unique_ptr<JoltContactListener> m_contactListener;

    // 衝突レイヤーのフィルタ。PhysicsSystem が参照し続けるので、デストラクタで PhysicsSystem を先に破棄する
    BPLayerInterfaceImpl              m_bpLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadphaseFilter;
    ObjectLayerPairFilterImpl         m_objectVsObjectFilter;

    bool m_isInitialized = false;
};