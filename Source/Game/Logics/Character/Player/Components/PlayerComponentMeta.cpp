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

COMPONENT_META_END(PlayerComponent)