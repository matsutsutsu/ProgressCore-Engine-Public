#pragma once
#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID

namespace CCL { namespace ECS { class World; } }

// =====================================================================
// Time ─ 時間スケール操作の統一ファサード
//
//   TimeContext / TimeStateComponent の生フィールドを直接叩かず、
//   「何を・どのスコープで・どれだけ・何秒」を意図で呼ぶ。中で正しい
//   フィールドを一括セットするので、片方だけ設定して壊す事故が起きない。
//   実際の dt 合成（rawDt × global × group × entity、ヒットストップは上書き）は
//   TimeScaleSystem(L01) が担当する。ここは「値の入れ方」だけを提供する。
//
//   全体スローは cinematicSlow の単一チャネルに集約し、複数要求は
//   「強い方(scale小)・長い方(timer大)が優先」で合成する（last-writer-wins をやめた）。
// =====================================================================
namespace Time
{
    // --- 全体（World） ---
    // 世界全体を一瞬止める（ヒットストップ）。freezeScale=0で完全停止、0.1で10%速度。長い方が優先。
    void HitStop(CCL::ECS::World &world, float seconds, float freezeScale = 0.0f);
    // 世界全体を一時スロー（イーズ復帰）。撃破スロー等の一発演出。強い方・長い方が優先。
    void SlowWorld(CCL::ECS::World &world, float scale, float seconds);
    // 「このフレーム、世界全体をスロー」。押し続ける間は毎フレーム呼ぶ（切り払い準備など）。
    //  呼ぶのをやめると easeOutSeconds 秒でイーズ復帰する。
    void SustainWorldSlow(CCL::ECS::World &world, float scale, float easeOutSeconds = 0.3f);
    // 世界スローを即解除（イーズを待たず等速へスナップ）。
    void ClearWorldSlow(CCL::ECS::World &world);

    // --- 個別（Entity） ---
    // 1体だけを一瞬止める（命中時のヒットストップ等）。長い方が優先。
    void HitStopEntity(CCL::ECS::World &world, CCL::ECS::EntityID id, float seconds, float freezeScale = 0.0f);

    // --- 制御 ---
    void Pause(CCL::ECS::World &world);
    void Resume(CCL::ECS::World &world);
    void StepFrame(CCL::ECS::World &world); // 一時停止中に1フレームだけ進める
}
