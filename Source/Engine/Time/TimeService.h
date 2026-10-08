#pragma once
// ===========================================================================
// 概要: 時間スケール操作の統一ファサード（名前空間 Time）。
//
// 責務:
//   - ヒットストップ・世界スロー・個別停止・一時停止を「意図」で呼べる API にする
//
// 備考:
//   TimeContext / TimeStateComponent の生フィールドを直接叩かず、
//   「何を・どのスコープで・どれだけ・何秒」を意図で呼ぶ。中で正しい
//   フィールドを一括セットするので、片方だけ設定して壊す事故が起きない。
//   実際の dt 合成（rawDt × global × group × entity、ヒットストップは上書き）は
//   TimeScaleSystem（L01）が担当する。ここは「値の入れ方」だけを提供する。
//
//   全体スローは cinematicSlow の単一チャネルに集約し、複数要求は
//   「強い方（scale 小）・長い方（timer 大）が優先」で合成する（後勝ちにはしない）。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID

namespace CCL
{
    namespace ECS
    {
        class World;
    }
} // namespace CCL

namespace Time
{
    // ============================================================
    // 世界全体
    // ============================================================

    // 世界全体を一瞬止める（ヒットストップ）。freezeScale=0 で完全停止、0.1 で10%速度。長い方が優先
    void HitStop(CCL::ECS::World &world, float seconds, float freezeScale = 0.0f);
    // 世界全体を一時スロー（イーズ復帰）。撃破スロー等の一発演出。強い方・長い方が優先
    void SlowWorld(CCL::ECS::World &world, float scale, float seconds);
    /**
     * @brief このフレーム、世界全体をスローにする。押し続ける間は毎フレーム呼ぶ（切り払い準備など）。
     * @param world          スローにする World。
     * @param scale          スローの倍率。
     * @param easeOutSeconds 呼ぶのをやめてから等速へ戻るまでの時間（秒）。
     */
    void SustainWorldSlow(CCL::ECS::World &world, float scale, float easeOutSeconds = 0.3f);
    // 世界スローを即解除する（イーズを待たず等速へ戻す）
    void ClearWorldSlow(CCL::ECS::World &world);

    // ============================================================
    // 個別の Entity
    // ============================================================

    // 1体だけを一瞬止める（命中時のヒットストップ等）。長い方が優先
    void HitStopEntity(CCL::ECS::World &world, CCL::ECS::EntityID id, float seconds, float freezeScale = 0.0f);

    // ============================================================
    // 一時停止・コマ送り（デバッグ用）
    // ============================================================

    void Pause(CCL::ECS::World &world);
    void Resume(CCL::ECS::World &world);
    void StepFrame(CCL::ECS::World &world); // 一時停止中に1フレームだけ進める

    // ============================================================
    // 描画ステージ用の dt
    // ============================================================

    /**
     * @brief 描画ステージで動くゲーム系 System（カメラ追従など）が使う dt。一時停止中は 0 を返す。
     *
     * 描画ステージはポーズ中も実時間で走るので、実時間 dt で減衰を進めると
     * 止まった世界へカメラだけが追いつこうとして動いてしまう。
     * スロー・ヒットストップは掛けない（カメラは実時間で動かした方が手触りが良い）。
     *
     * @param world  時計を持つ World。
     * @param realDt 描画ステージに渡された実時間 dt（秒）。
     * @return 一時停止中は 0、それ以外は realDt。
     */
    float PresentationDt(CCL::ECS::World &world, float realDt);
} // namespace Time
