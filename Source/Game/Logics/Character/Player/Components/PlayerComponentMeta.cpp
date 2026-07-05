/// @file PlayerComponentMeta.cpp
/// @brief PlayerComponentのシリアライズおよびインスペクタGUI用のメタデータ定義

#include "PlayerComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

// ===========================================================================
// PlayerComponent メタデータ定義
// ★修正: BEGIN_FULL を使用
// ===========================================================================
COMPONENT_META_BEGIN_FULL(PlayerComponent, "Player Component", CCL::Categories::Gameplay, 130, false)

CFIELD(turnSpeed, u8"旋回速度 (turnSpeed)", "Movement")

CFIELD(maxJumps, u8"最大ジャンプ回数 (maxJumps)", "Jump Settings")
CFIELD(currentJumpCount, u8"現在のジャンプ回数 (currentJumpCount)", "Jump State")
CFIELD(wasJumpButtonPressed, u8"前フレームボタン状態 (wasJumpButtonPressed)", "Jump State")

CFIELD_FLOAT_RANGED(coyoteTimeWindow, u8"コヨーテタイム (coyoteTimeWindow)", 0.01f, 0.0f, 1.0f, "Game Feel")
CFIELD_FLOAT_RANGED(jumpBufferWindow, u8"先行入力バッファ (jumpBufferWindow)", 0.01f, 0.0f, 1.0f, "Game Feel")
CFIELD(timeSinceGrounded, u8"滞空時間 (timeSinceGrounded)", "State")
CFIELD(jumpBufferTimer, u8"ジャンプ予約タイマー (jumpBufferTimer)", "State")
CFIELD(currentYaw, u8"現在のYaw角 (currentYaw)", "State")
CFIELD(currentRoll, u8"現在のRoll角 (currentRoll)", "State")

CFIELD(wasGroundedLastFrame, u8"前フレーム接地状態 (wasGroundedLastFrame)", "State")
CFIELD(lastFallVelocity, u8"直前落下速度 (lastFallVelocity)", "State")

CFIELD_FLOAT_RANGED(bankingMultiplier, u8"バンキング係数 (bankingMultiplier)", 0.1f, 0.0f, 5.0f, "Visuals")
CFIELD_FLOAT_RANGED(maxBankingAngle, u8"最大バンキング角 (maxBankingAngle)", 1.0f, 0.0f, 90.0f, "Visuals")

COMPONENT_META_END(PlayerComponent)