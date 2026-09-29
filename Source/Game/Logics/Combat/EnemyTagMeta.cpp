// Tag::EnemyTag のシリアライズ/インスペクタ定義。
//
// メンバが無いのにメタを書くのは、メタが無いとプレハブの JSON に載せられず、
// コードから AddComponent するしかなくなるため。敵かどうかはデータで決められるようにする
#include "Game/Logics/Combat/EnemyTag.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// REGISTER_COMPONENT は型名をつないで識別子を作る（AutoReg_##T）ので、名前空間付きの型名を
// そのまま渡せない（AutoReg_Tag::EnemyTag は識別子にならない）。そこで別名を1つ挟む。
// 別名が指す型は Tag::EnemyTag のままなので、ComponentMeta の特殊化先も Tag::EnemyTag になる
using EnemyTag = Tag::EnemyTag;

COMPONENT_META_BEGIN_CAT(EnemyTag, "EnemyTag", CCL::Categories::Meta)
COMPONENT_META_END(EnemyTag)
