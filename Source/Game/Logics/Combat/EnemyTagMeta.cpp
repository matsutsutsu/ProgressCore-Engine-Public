// Tag::EnemyTag のシリアライズ/インスペクタ定義。
//
// フィールドを1つも持たないのにメタが要る理由:
//   メタが無いとプレハブのJSONに載せられず、コードから AddComponent するしか
//   なくなる。「この敵は敵である」はデータ側で言えるべきなので登録しておく。
#include "Game/Logics/Combat/EnemyTag.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// REGISTER_COMPONENT は型名をトークン連結して識別子を作る（AutoReg_##T）ため、
// 名前空間で修飾した型名をそのまま渡せない（AutoReg_Tag::EnemyTag は不正な識別子）。
// 別名を1つ挟んで回避する。指す型は Tag::EnemyTag のままなので、
// ComponentMeta の特殊化先も Tag::EnemyTag と同一。
using EnemyTag = Tag::EnemyTag;

COMPONENT_META_BEGIN_CAT(EnemyTag, "EnemyTag", CCL::Categories::Meta)
COMPONENT_META_END(EnemyTag)
