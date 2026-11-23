#pragma once
#include "Engine/Serialization/Core/EntityLink.h"

// プレイヤーの設定値と実行時の状態。これを持つエンティティがプレイヤーとして扱われる
struct PlayerComponent
{
    float turnSpeed = 10.0f; // 移動の向きへ向き直る速さ（1/秒）

    // --- 走り（回避ダッシュの後に移る）---

    float runSpeed           = 24.0f; // 走る速さ（m/秒）
    float runIdleReleaseTime = 0.4f;  // 無入力がこの長さ続いたら歩きに戻す（秒）
    bool  isRunning          = false; // 走っているか。ダッシュ攻撃の条件にも使う
    float runIdleTimer       = 0.0f;  // 無入力が続いている長さ（秒）

    // --- 2段ジャンプ ---

    int maxJumps         = 2; // 着地までに跳べる回数
    int currentJumpCount = 0; // 着地してから跳んだ回数

    // --- 向きと旋回時の傾き ---

    float bankingMultiplier = 0.5f;  // 傾きの強さ
    float maxBankingAngle   = 30.0f; // 傾きの上限（度）
    float currentYaw        = 0.0f;  // 向き（rad）。0 が +Z。物理へはこれだけ渡す
    float currentRoll       = 0.0f;  // 旋回時の傾き（rad）。見た目にだけ付ける

    // --- 空中の判定（PlayerJumpSystem が毎ステップ書く）---

    bool  isAirborne        = false; // 空中か。空中攻撃の分岐などはこれを見る
    float timeSinceGrounded = 0.0f;  // 接地を離れてからの長さ（秒）。0 は接地中
    float airborneGraceTime = 0.12f; // 接地を離れても、この長さまでは空中と判定しない（秒）

    // --- 攻撃時に敵へ向く（PlayerAttackSystem が書き、PlayerFacingSystem が回す）---

    float attackFaceYaw          = 0.0f;  // 向く先の yaw（rad）
    float attackFaceTimer        = 0.0f;  // 向く残りの長さ（秒）。この間は移動の向きより優先
    float attackFaceMaxDistance  = 5.0f;  // この距離以内の敵にだけ向く（m）
    float attackFaceConeAngle    = 70.0f; // ロックオンしていないとき、向く敵を探す前方の扇の半角（度）
    float attackFaceDuration     = 0.22f; // 敵の方へ向き直る長さ（秒）
    float attackBackFaceDuration = 0.8f;  // 後ろの技で、敵を向いたまま下がる長さ（秒）

    // --- 攻撃の入力（PlayerAttackSystem が読む）---

    float attackInputBuffer    = 0.25f; // 押した攻撃を覚えておく長さ（秒）。この間に受付区間が開けば次の技が出る
    float attackStickThreshold = 0.5f;  // スティックをこれより深く倒したときだけ、前・後ろの技にする（0〜1）
    float attackDirConeAngle   = 50.0f; // 敵の方・敵と反対の方と判定する扇の半角（度）

    // --- ロックオン ---

    CCL::Serialization::EntityLink lockOnTargetID = CCL::ECS::InvalidEntityID; // ロックオン中の敵
};
