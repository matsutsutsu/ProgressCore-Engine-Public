#include "Game/Logics/Combat/Feedback/DefeatSlowMotionSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"
#include "Engine/Cinematic/CinematicEvents.h"

using namespace CCL::ECS;

namespace
{
    // 撃破ムービーの台本
    constexpr const char *kDefeatCinematic = "Assets/Cinematics/BossDefeat.json"; 
}



// ボスが倒れた瞬間にスローをかけ、スローが明けたら撃破ムービーを頼む
void DefeatSlowMotionSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 倒れたボスを1体につき1回だけ見つけて、スローをかける
    // 2. スローが明けたら、撃破ムービーを頼む

    // ---------------------------------------------------------
    // 1. スロー
    // ---------------------------------------------------------

    ForEach([&](HealthComponent &hp, const BossPhaseComponent &phase) {
        // 倒れたボスを1回だけ拾う
        // 倒れた後も消えずに残るので、フラグで見分ける
        if (hp.deathFxFired || hp.currentHealth > 0.0f) return;
        hp.deathFxFired = true;

        // 
        Time::SlowWorld(*m_world, phase.defeatSlowScale, phase.defeatSlowDuration);

        // スローを通常のカメラで見せてから、ムービーを頼む
        // ムービーの開始待ちはカメラを奪って最初の絵で止めるので、スローの間には頼めない
        if (!m_defeatMovieArmed)
        {
            m_defeatMovieArmed = true;
            m_defeatMovieTimer = phase.defeatSlowDuration;
        }
    });

    // ---------------------------------------------------------
    // 2. 撃破ムービー
    // ---------------------------------------------------------

    // dt はスローのかからない実時間。スローの長さと同じ時計で数える
    if (m_defeatMovieArmed && m_defeatMovieTimer > 0.0f)
    {
        m_defeatMovieTimer -= dt;
        if (m_defeatMovieTimer <= 0.0f)
            m_world->GetEventBus().Publish(CinematicPlayRequestEvent{kDefeatCinematic});
    }
}



// L06（ダメージの処理）で倒れたかが決まった後に見る
REGISTER_LOGIC_SYSTEM(DefeatSlowMotionSystem, Priority::LogicStage::L07_Cleanup);
