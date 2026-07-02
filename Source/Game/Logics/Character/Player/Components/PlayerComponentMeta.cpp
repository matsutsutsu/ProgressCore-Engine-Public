/// @file PlayerComponentMeta.cpp
/// @brief PlayerComponentのシリアライズおよびインスペクタGUI用のメタデータ定義

#include "PlayerComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// PlayerComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN(PlayerComponent, "Player Component")

// 回転・システム関連のパラメータ
CFIELD(turnSpeed, u8"旋回速度 (turnSpeed)", "Movement")

// 2段ジャンプ設定・状態
CFIELD(maxJumps, u8"最大ジャンプ回数 (maxJumps)", "Jump Settings")
CFIELD(currentJumpCount, u8"現在のジャンプ回数 (currentJumpCount)", "Jump State")
CFIELD(wasJumpButtonPressed, u8"前フレームボタン状態 (wasJumpButtonPressed)", "Jump State")

// 操作ハック (Game Feel)
CFIELD_FLOAT_RANGED(coyoteTimeWindow, u8"コヨーテタイム (coyoteTimeWindow)", 0.01f, 0.0f, 1.0f, "Game Feel")
CFIELD_FLOAT_RANGED(jumpBufferWindow, u8"先行入力バッファ (jumpBufferWindow)", 0.01f, 0.0f, 1.0f, "Game Feel")
CFIELD(timeSinceGrounded, u8"滞空時間 (timeSinceGrounded)", "State")
CFIELD(jumpBufferTimer, u8"ジャンプ予約タイマー (jumpBufferTimer)", "State")

// 着地衝撃用のステート
CFIELD(wasGroundedLastFrame, u8"前フレーム接地状態 (wasGroundedLastFrame)", "State")
CFIELD(lastFallVelocity, u8"直前落下速度 (lastFallVelocity)", "State")

// 視覚的慣性 (バンキング)
CFIELD_FLOAT_RANGED(bankingMultiplier, u8"バンキング係数 (bankingMultiplier)", 0.1f, 0.0f, 5.0f, "Visuals")
CFIELD_FLOAT_RANGED(maxBankingAngle, u8"最大バンキング角 (maxBankingAngle)", 1.0f, 0.0f, 90.0f, "Visuals")
CFIELD(currentYaw, u8"現在のYaw角 (currentYaw)", "State")
CFIELD(currentRoll, u8"現在のRoll角 (currentRoll)", "State")



COMPONENT_META_END(PlayerComponent)