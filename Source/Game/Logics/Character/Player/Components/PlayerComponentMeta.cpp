/// @file PlayerComponentMeta.cpp
/// @brief PlayerComponentのシリアライズおよびインスペクタGUI用のメタデータ定義

#include "PlayerComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// PlayerComponent メタデータ定義
// BEGIN_FULL を使用
// ===========================================================================
COMPONENT_META_BEGIN_FULL(PlayerComponent, "Player Component", CCL::Categories::Gameplay, 130, false)

CFIELD(turnSpeed, "旋回速度", "移動")

CFIELD(maxJumps, "最大ジャンプ回数", "ジャンプ設定")
CFIELD(currentJumpCount, "現在のジャンプ回数", "ジャンプ状態")
CFIELD(wasJumpButtonPressed, "前フレームボタン状態", "ジャンプ状態")

CFIELD(currentYaw, "現在のYaw角", "状態")
CFIELD(currentRoll, "現在のRoll角", "状態")

CFIELD(wasGroundedLastFrame, "前フレーム接地状態", "状態")
CFIELD(lastFallVelocity, "直前落下速度", "状態")
CFIELD(isAirborne, "空中状態", "状態")

CFIELD_FLOAT_RANGED(bankingMultiplier, "バンキング係数", 0.1f, 0.0f, 5.0f, "見た目")
CFIELD_FLOAT_RANGED(maxBankingAngle, "最大バンキング角", 1.0f, 0.0f, 90.0f, "見た目")

CFIELD_FLOAT_RANGED(attackFaceMaxDistance, "攻撃時オートフェイス距離", 0.5f, 0.0f, 30.0f, "攻撃")

COMPONENT_META_END(PlayerComponent)