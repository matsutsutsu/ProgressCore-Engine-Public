#include "PreviewGridComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(PreviewGridComponent, "Preview Grid", CCL::Categories::Debug, 900, false)
CFIELD(visible, "表示", "目盛り")
CFIELD_FLOAT_RANGED(spacing, "間隔 [m]", 0.05f, 0.1f, 10.0f, "目盛り")
CFIELD_INT_RANGED(halfCount, "片側の本数", 1, 200, "目盛り")
CFIELD_INT_RANGED(majorEvery, "太線の間隔（本）", 1, 50, "目盛り")
CFIELD_COLOR(color, "線の色", "目盛り")
CFIELD_COLOR(majorColor, "太線の色", "目盛り")
COMPONENT_META_END(PreviewGridComponent)
