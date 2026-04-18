#include "AIHearingSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Platform/Logger.h"
#include "SimpleMath.h"

// システム登録用のマクロヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

void AIHearingSystem::Initialize() {
    // エンジンの規約に従い、Initialize内でイベントを購読する
    ListenEvent<AISoundEvent>([this](const AISoundEvent& e) {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_frameEvents.push_back(e);
        });
}

void AIHearingSystem::Update(float dt) {
    // 1. 今フレームで発生した音イベントを安全にコピーしてクリア
    std::vector<AISoundEvent> eventsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        eventsToProcess = m_frameEvents;
        m_frameEvents.clear();
    }

    // 音が鳴っていなければ何もしない（超高速リターン）
    if (eventsToProcess.empty()) return;

    // 2. 全AIの聴覚判定ループ
    ForEachWithID([&](EntityID aiEntity,
        const TransformComponent& transform,
        const AIPerceptionComponent& perception,
        AIMemoryComponent& memory,
        AIStateComponent& state,
        NavAgentComponent& navAgent) {

            // 聴力が0（Day1, Day2）の場合は聞こえない
            if (perception.hearingRadius <= 0.0f) return;

            // すでにプレイヤーを視認して追跡中(Chase)の場合は、音より視覚を優先する
            if (state.currentState == AIState::Chase) return;

            Vector3 aiPos = transform.position;
            bool heardSomething = false;
            Vector3 loudestSoundPos = Vector3::Zero;
            float maxPriority = -1.0f;

            // 発生した全ての音に対して判定
            for (const auto& ev : eventsToProcess) {
                float dist = Vector3::Distance(aiPos, ev.position);

                // AIの耳が届く距離 かつ 音のボリューム半径内 にいるか
                if (dist <= perception.hearingRadius && dist <= ev.volumeRadius) {

                    // 最も近い（優先度の高い）音源を特定する
                    float priority = perception.hearingRadius - dist;
                    if (priority > maxPriority) {
                        maxPriority = priority;
                        loudestSoundPos = ev.position;
                        heardSomething = true;
                    }
                }
            }

            // ---------------------------------------------------------
            // 🧠 音に対するリアクション（Investigateへの遷移）
            // ---------------------------------------------------------
            if (heardSomething) {
                // 記憶を音の発生源で上書き
                memory.lastKnownPlayerPos = loudestSoundPos;
                // 姿を見ていないので疑心暗鬼度は半分程度
                memory.suspicionLevel = 50.0f;

                // 調査モードへ移行
                state.currentState = AIState::Investigate;
                state.timeInState = 0.0f;

                navAgent.targetPosition = loudestSoundPos;
                navAgent.isPathRequested = true;

                CCL_LOG_INFO(LogCategory::Core, "AI[%llu] Heard a Sound! Investigating: (%.1f, %.1f, %.1f)",
                    aiEntity, loudestSoundPos.x, loudestSoundPos.y, loudestSoundPos.z);
            }
        });
}

// システム登録：聴覚(Hearing) -> 視覚(Perception) -> 意思決定(Patrol) の順で実行
REGISTER_LOGIC_SYSTEM(AIHearingSystem, Priority::LogicStage::L02_Update)