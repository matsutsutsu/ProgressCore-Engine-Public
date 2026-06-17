/// @file PlayerComponent.cpp
/// @brief PlayerComponentのシリアライズおよびインスペクタGUI用のメタデータ定義

#include "PlayerComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// PlayerComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN(PlayerComponent, "Player Component")

// 移動関連のパラメータ
// CFIELD(変数名, "インスペクタ表示名", "カテゴリ名")
CFIELD(moveSpeed, "Move Speed", "Movement")
CFIELD(turnSpeed, "Turn Speed", "Movement")

// 入力・システム関連のパラメータ
CFIELD(padSlot, "Pad Slot", "Input")

COMPONENT_META_END(PlayerComponent)