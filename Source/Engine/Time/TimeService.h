#pragma once
// ===========================================================================
// 概要: ヒットストップ・世界のスロー・個別の停止・一時停止を呼ぶ入口（名前空間 Time）
//
// 備考: TimeContext / TimeStateComponent の欄は直接書かずにここを通す。関係する欄をまとめて書くので、片方だけ書き忘れない
//       dt の合成は TimeScaleSystem（L01）がする。ここは値の入れ方だけ
//       全体のスローは cinematicSlow の1つにまとめ、要求が重なったら強い方（scale が小さい）と長い方を残す
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"

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
    // このフレーム、世界全体をスローにする。続けたい間は毎フレーム呼ぶ
    // easeOutSeconds は、呼ぶのをやめてから等速に戻るまでの時間（秒）
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

    // 描画の段で動くゲームの System（カメラの追従など）の dt。一時停止中は 0、それ以外は realDt
    // 描画の段はポーズ中も実時間で走るので、realDt のままだと止まった世界へカメラだけが追いつこうとする
    // スローとヒットストップは掛けない（カメラは実時間で動かす方が手触りがよい）
    float PresentationDt(CCL::ECS::World &world, float realDt);
} // namespace Time
