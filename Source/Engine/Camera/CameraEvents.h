#pragma once
// ===========================================================================
// CameraEvents.h
// --------------
// 役割:
//   「カメラにこうしてほしい」という演出要求イベントを Engine 側で定義する。
//
// 設計方針:
//   カメラは要求の“理由”（ジャスト回避なのか着地なのか）を知る必要がない。
//   ゲーム側が自分の文脈をここで定義された汎用の要求へ翻訳して発行することで、
//   カメラがゲーム固有のイベント型を include せずに済む（Engine→Game 依存の遮断）。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"

/// @brief 画面を揺らす要求。着地・被弾など発生源は問わない。
struct CameraShakeRequestEvent
{
    float amplitude; ///< 揺れの強さ
    float duration;  ///< 揺れる時間（秒）
};

/// @brief 指定エンティティを追従しているカメラの画角を瞬間的に変える要求（FOVパンチ）。
///        設定後は CameraBodyTPSComponent::fovDamping により自動的に元へ戻る。
struct CameraFovPunchRequestEvent
{
    CCL::ECS::EntityID targetID; ///< このエンティティを追従しているカメラが対象
    float              fov;      ///< 瞬間的に設定する画角（度）
};
