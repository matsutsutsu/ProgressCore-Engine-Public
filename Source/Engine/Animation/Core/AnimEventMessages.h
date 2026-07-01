#pragma once
// ===================================================================================
// ファイル: AnimEventMessages.h
// 役割       : アニメーションから他システムへ送る「通知（イベント）メッセージ」定義。
// 責務       : AnimationSystem が台本の指定時刻に到達した際、EventBus を介して
//              音響・エフェクト・空間ヒットボックス生成等の処理を依頼するデータ。
// 位置付け   : アニメーションと各システムを疎結合に保つための一方向通知。
//              EventBus::Publish は同期即時配送のため、購読側は軽量処理に留めること。
// ===================================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "SimpleMath.h"
#include <string>

/// @brief 攻撃判定 ON/OFF 通知（点イベント方式を使う場合用。現在は区間ポーリングが主）。
struct AnimEventHitBoxMessage
{
    CCL::ECS::EntityID entity;
    bool               isActive; ///< true で判定ON、false で判定OFF
};

/// @brief 音響システム宛て。FMOD イベント再生を依頼する。
struct AnimEventSoundMessage
{
    CCL::ECS::EntityID entity;
    std::string        eventPath; ///< FMOD イベントパス（例: "event:/SFX/SwordSwing"）
};

/// @brief エフェクトシステム宛て。Effekseer エフェクト生成を依頼する。
struct AnimEventEffectMessage
{
    CCL::ECS::EntityID           entity;
    std::string                  effectPath; ///< .efk / .efkefc パス
    DirectX::SimpleMath::Vector3 offset;     ///< 発生位置オフセット
    float                        scale;      ///< スケール倍率
    bool                         follow;     ///< 親(キャラ)に追従するか
};

/// @brief 戦闘システム宛て。空間に独立した球状ヒットボックスの生成を依頼する。
struct AnimEventSpawnHitboxMessage
{
    CCL::ECS::EntityID           owner;           ///< 攻撃の帰属（このアニメを再生中のエンティティ）
    DirectX::SimpleMath::Vector3 offset;          ///< 生成位置オフセット
    float                        radius;          ///< 球半径
    float                        damage;          ///< 攻撃力
    float                        hitStopDuration; ///< ヒットストップ時間
};