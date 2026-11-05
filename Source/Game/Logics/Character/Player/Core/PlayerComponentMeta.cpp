#include "PlayerComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(PlayerComponent, "Player Component", CCL::Categories::Gameplay, 130, false)

// --- 調整パラメータ（人が決める値。保存する）-----------------------------
CFIELD(turnSpeed, "旋回速度", "移動")

CFIELD_FLOAT_RANGED(runSpeed, "走行速度", 0.5f, 0.0f, 200.0f, "移動")
CFIELD_FLOAT_RANGED(runIdleReleaseTime, "走行の無入力解除時間", 0.05f, 0.0f, 5.0f, "移動")
CFIELD_FLOAT_RANGED(airborneGraceTime, "空中判定の猶予", 0.01f, 0.0f, 1.0f, "移動")

CFIELD(maxJumps, "最大ジャンプ回数", "ジャンプ設定")

CFIELD_FLOAT_RANGED(bankingMultiplier, "バンキング係数", 0.1f, 0.0f, 5.0f, "見た目")
CFIELD_FLOAT_RANGED(maxBankingAngle, "最大バンキング角", 1.0f, 0.0f, 90.0f, "見た目")

CFIELD_FLOAT_RANGED(attackFaceMaxDistance, "攻撃時オートフェイス距離", 0.5f, 0.0f, 30.0f, "攻撃")

// --- 実行時状態（書き手はシステム。表示はするが保存しない）-----------------
//   保存すると、保存した瞬間の値がプレハブに残り、そこから生成した個体が
//   その値から始まる（例: 保存時の向きのまま開始する）
//   開始時の向きは BattleStartFacingSystem がボスとの位置関係から決める
CFIELD_RT(isRunning, "走行中", "状態")
CFIELD_RT(currentJumpCount, "現在のジャンプ回数", "状態")
CFIELD_RT(currentYaw, "現在のYaw角(rad)", "状態")
CFIELD_RT(currentRoll, "現在のRoll角(rad)", "状態")
CFIELD_RT(isAirborne, "空中状態", "状態")
CFIELD_RT(timeSinceGrounded, "接地離れ経過時間", "状態")

COMPONENT_META_END(PlayerComponent)