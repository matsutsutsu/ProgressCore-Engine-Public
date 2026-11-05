#include "PlayerInputComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// 表示順は Player Component(130) の直前にする（入力 → 状態 の順に読めるようにするため）
COMPONENT_META_BEGIN_FULL(PlayerInputComponent, "Player Input", CCL::Categories::Gameplay, 125, false)

CFIELD_RO(attackPressed, "攻撃(押した瞬間)", "入力", nullptr)
CFIELD_RO(jumpPressed, "ジャンプ(押した瞬間)", "入力", nullptr)
CFIELD_RO(dashPressed, "回避(押した瞬間)", "入力", nullptr)
CFIELD_RO(lockOnPressed, "ロックオン(押した瞬間)", "入力", nullptr)

COMPONENT_META_END(PlayerInputComponent)
