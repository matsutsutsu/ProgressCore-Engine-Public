#pragma once
// ===========================================================================
// 概要: アニメーションの点イベントを他の System へ送るメッセージの定義。
//
// 責務:
//   - AnimationSystem がクリップの指定時刻に達したとき、EventBus で
//     音・エフェクト・判定球の生成を依頼するためのデータ
//
// 備考: EventBus::Publish はその場で購読側を呼ぶので、購読側は軽い処理に留めること。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "SimpleMath.h"
#include <string>

// サウンドの再生を依頼する。
// TODO(2026-09-25): 購読する System が無く、現在は鳴らない。AudioSystem で受けて Entity の位置で鳴らす
struct AnimEventSoundMessage
{
    CCL::ECS::EntityID entity;
    std::string        eventPath; // サウンドのイベント名（AudioConfigDXTK.json に登録した名前）
};

// Effekseer エフェクトの生成を依頼する
struct AnimEventEffectMessage
{
    CCL::ECS::EntityID           entity;
    std::string                  effectPath; // .efk / .efkefc パス
    DirectX::SimpleMath::Vector3 offset;     // 発生位置のオフセット
    float                        scale;      // スケール倍率
    bool                         follow;     // 親（キャラ）に追従するか
};

// 空間に独立した球状の判定（ヒットボックス）の生成を依頼する。
//   offset は owner のローカル座標（前方 = キャラの向き）で、購読側で owner の回転を掛けて
//   ワールド位置にする。lifeTime はシーケンサーのブロック長（endTime - startTime）
struct AnimEventSpawnHitboxMessage
{
    CCL::ECS::EntityID           owner;           // 攻撃の持ち主（このアニメを再生している Entity）
    DirectX::SimpleMath::Vector3 offset;          // 生成位置のオフセット（owner のローカル座標）
    float                        radius;          // 球の半径
    float                        damage;          // 攻撃力
    float                        hitStopDuration; // ヒットストップ時間（秒）
    float                        hitStopScale;    // ヒットストップ中のタイムスケール（0 で完全停止）
    float                        lifeTime;        // 判定球の生存時間（秒）
    uint8_t                      attackLayer;     // 判定球の物理レイヤー番号（誰に当てるか）
    float                        knockback;       // ノックバックの強さ（0 で無効）
};