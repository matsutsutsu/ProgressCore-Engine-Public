#include "AIHearingSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Platform/Logger.h"
#include "SimpleMath.h"

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

// ===================================================================================
// 【 AI感覚器 (耳) : AIHearingSystem 】
//
// [ 役割 ]
// 空間で発生した「音の波紋(Event)」を検知し、AIを不審な場所へ向かわせる。
//
// [ ECSデータパイプライン ]
// 📥 READ  : TransformComponent      (AI自身の位置)
// 📥 READ  : AIPerceptionComponent   (AIの聴力：聞き取れる半径)
// 📤 WRITE : AIMemoryComponent       (音が鳴った座標を記憶、疑心暗鬼レベルの更新)
// 📤 WRITE : AIStateComponent        (音を聞いた時に Investigate(調査) へ状態遷移)
// 📤 WRITE : NavAgentComponent       (目標座標を音が鳴った位置へ更新)
//
// [ 内部挙動の直感的な解説 ]
// プレイヤーや環境が発した AISoundEvent を EventBus 経由で受け取ります。
// AI自身の座標から音源までの距離を測り、「音の大きさ」と「AIの聴力」の範囲内であれば検知します。
// 視覚(Chase)が最も優先されるため、既にプレイヤーを追いかけている最中は音を無視します。
// ===================================================================================

void AIHearingSystem::Initialize() {
    // 空間で発生した音をすべて集約する
    ListenEvent<AISoundEvent>([this](const AISoundEvent& e) {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_frameEvents.push_back(e);
        });
}

void AIHearingSystem::Update(float dt) {
    // 1. フレーム内のイベントをスワップしてロック時間を最小化する
    std::vector<AISoundEvent> currentEvents;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        currentEvents = std::move(m_frameEvents);
        m_frameEvents.clear();
    }

    if (currentEvents.empty()) return;

    // 2. AIごとに聞こえたかどうかを判定 (DOD: Chunk単位の並列処理)
    ForEachWithID([&](EntityID aiEntity,
        const TransformComponent& transform,
        const AIPerceptionComponent& perception,
        AIMemoryComponent& memory,
        AIStateComponent& state,
        NavAgentComponent& navAgent) {

            // Day1-2 などで聴力が解放されていない場合はスキップ
            if (perception.hearingRadius <= 0.0f) return;

            // 視覚ですでにプレイヤーを追跡中(Chase)なら、音の調査は行わない
            if (state.currentState == AIState::Chase) return;

            Vector3 aiPos = transform.position;
            bool heardSound = false;
            Vector3 bestSoundPos = Vector3::Zero;
            float highestPriority = -1.0f;

            for (const auto& ev : currentEvents) {
                float dist = Vector3::Distance(aiPos, ev.position);

                // 「AIの地獄耳の良さ」と「音の大きさ」の積で判定
                if (dist <= perception.hearingRadius && dist <= ev.volumeRadius) {
                    // 最も「気になる（近い）」音を採用する
                    float priority = perception.hearingRadius - dist;
                    if (priority > highestPriority) {
                        highestPriority = priority;
                        bestSoundPos = ev.position;
                        heardSound = true;
                    }
                }
            }

            // ---------------------------------------------------------
            // 🧠 リアクション: 調査(Investigate)への遷移
            // ---------------------------------------------------------
            if (heardSound) {
                // ★修正: 新しい変数名へ変更
                memory.lastKnownPos = bestSoundPos;

                // ★修正: std::max は両辺の型が完全に一致している必要があるため、50.0f と明記する
                memory.currentAngerLevel = (std::max)(memory.currentAngerLevel, 50.0f);

                state.currentState = AIState::Investigate;
                state.timeInState = 0.0f;

                navAgent.targetPosition = bestSoundPos;
                navAgent.isPathRequested = true;

                CCL_LOG_INFO(LogCategory::Core, "AI[%llu] Heard something. Moving to Investigate.", aiEntity);
            }
        });
}

// 規約に基づき、論理システムとして登録
REGISTER_LOGIC_SYSTEM(AIHearingSystem, Priority::LogicStage::L02_AI_Sense)