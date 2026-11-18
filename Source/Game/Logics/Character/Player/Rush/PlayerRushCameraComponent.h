#pragma once
#include "Game/Logics/Camera/PlayerCameraComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"

// フェーズ1つ分の構図。blendTime と bounce 以外は、普段のカメラへの足し込み量
struct RushCameraShot
{
    float distance    = 0.0f; // 距離の足し込み（m）。負で寄る
    float fov         = 0.0f; // 画角の足し込み（度）
    float pitch       = 0.0f; // ピッチの足し込み（度）。正で見上げる
    float yaw         = 0.0f; // ヨーの足し込み（度）
    float roll        = 0.0f; // 画面の傾き（度）
    float focus       = 0.0f; // 注視点を敵へ寄せる割合（0〜1）
    float followBoost = 0.0f; // 追従の速さの足し込み（1/秒）
    float blendTime   = 0.1f; // この構図へ寄せきる時間（秒）
    float bounce      = 0.0f; // 寄せたときの行き過ぎ（0〜0.8）。0 で行き過ぎない
};

// ラッシュ中のカメラ演出の設定と状態。フェーズは PlayerRushComponent から読むだけ
struct PlayerRushCameraComponent
{
    // --- フェーズごとの構図 ---

    // 値はプレハブで決める。ここで持つと、プレハブで詰めた値とずれた古い値が残って紛らわしい
    // 何も入れなければ足し込みは 0 で、そのフェーズは普段の構図のまま
    RushCameraShot locked; // 回避離脱
    RushCameraShot ready;  // 待機
    RushCameraShot attack; // 攻撃（詰めと詰めの斬り）
    RushCameraShot flurry; // 連撃

    // --- 終了・瞬間の演出 ---

    float releaseTime     = 0.6f;  // 終了後、普段の構図へ戻しきる時間（秒）
    bool  yawFollowsEvade = true;  // 回避の向きでヨーと傾きを左右反転するか
    float startShake      = 0.35f; // 回避成立の瞬間の揺れ（0〜1）
    float strikeFovPunch  = 10.0f; // 斬った瞬間に画角を広げる量（度）
    float strikeShake     = 0.5f;  // 斬った瞬間の揺れ（0〜1）

    // --- 実行時の状態 ---

    CameraFramingParams current{};                // 今カメラへ出している構図
    CameraFramingParams velocity{};               // current の各値の速さ。focusTarget は使わない
    float               weight         = 0.0f;  // 構図の効き（0〜1）
    float               yawSign        = 1.0f;  // ヨーと傾きの左右。カメラから見て左へ避けたら +1、右なら -1
    bool                wasActive      = false; // 前フレームにラッシュ中だったか
    bool                wasAttackFired = false; // 前フレームに斬り済みだったか
};
