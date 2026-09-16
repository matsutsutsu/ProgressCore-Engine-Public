// KnockbackComponent のシリアライズ/インスペクタ定義。
// ランタイム値(velocity/timer)は Fields() に載せない＝保存対象外。エディタで
// エンティティに追加できるようにするための登録もここで行う。
//
// 表示名は JSON の保存キーそのもの（REGISTER_COMPONENT に displayName が渡る）。
// 他の全コンポーネントと同じ英語表記にしておかないと、プレハブ側のキーと
// 一致せず読み込み時に黙って捨てられる。

#include "KnockbackComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// KnockbackComponent メタデータ定義（Movement カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(KnockbackComponent, "Knockback", CCL::Categories::Movement, 155, false)
CFIELD_FLOAT_RANGED(duration, "継続時間(秒)", 0.005f, 0.0f, 2.0f, "ノックバック")
CFIELD_FLOAT_RANGED(stunDuration, "硬直時間(秒)", 0.005f, 0.0f, 2.0f, "ノックバック")
CFIELD_RO(stunTimer, "硬直の残り(秒)", "ノックバック", "0より大きい間はプレイヤー操作を受け付けない。")
COMPONENT_META_END(KnockbackComponent)
