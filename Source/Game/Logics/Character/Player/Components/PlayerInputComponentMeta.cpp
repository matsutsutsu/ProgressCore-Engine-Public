/// @file PlayerInputComponentMeta.cpp
/// @brief PlayerInputComponent のシリアライズおよびインスペクタGUI定義。
/// @note  全て毎フレーム再計算される実行時状態のため、インスペクタでは読み取り専用にする。
///        保存対象から外さないのは、プレハブがこのコンポーネントの有無を
///        components のキーで表現しているため（キーが無い＝付いていない）。

#include "PlayerInputComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// PlayerInputComponent メタデータ定義
//   Player Component(130) の直前に並べる（入力 → 状態 の読み順にするため）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(PlayerInputComponent, "Player Input", CCL::Categories::Gameplay, 125, false)

CFIELD_RO(attackPressed, "攻撃(押した瞬間)", "入力", nullptr)
CFIELD_RO(jumpPressed, "ジャンプ(押した瞬間)", "入力", nullptr)
CFIELD_RO(dashPressed, "回避(押した瞬間)", "入力", nullptr)
CFIELD_RO(cleavePressed, "切り払い準備(押した瞬間)", "入力", nullptr)
CFIELD_RO(lockOnPressed, "ロックオン(押した瞬間)", "入力", nullptr)

COMPONENT_META_END(PlayerInputComponent)
