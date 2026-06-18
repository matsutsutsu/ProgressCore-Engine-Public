/// @file  KnockbackComponentMeta.cpp
/// @brief KnockbackComponent のシリアライズ/インスペクタ定義。
///        ノックバック(吹っ飛び)の速度ベクトル、減衰率、および稼働フラグを編集可能にする。

#include "KnockbackComponent.h" // ※プロジェクトの実際のパスに合わせて調整してください
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// KnockbackComponent メタデータ定義 (パターンA: 全自動)
// ===========================================================================
COMPONENT_META_BEGIN(KnockbackComponent, "Knockback")

// Vector3 には高速描画用の CFIELD_FLOAT3_FAST を使用
CFIELD_FLOAT3_FAST(velocity, "Velocity (XZ)", 0.1f, "Knockback State")

// 減衰率は 0.0 ~ 1.0 の範囲でスライダー化
CFIELD_FLOAT_RANGED(decayPerSec, "Decay Per Sec", 0.01f, 0.0f, 1.0f, "Knockback Settings")

// bool 型は基本の CFIELD で自動推論させる
CFIELD(isActive, "Is Active", "Knockback State")

COMPONENT_META_END(KnockbackComponent)