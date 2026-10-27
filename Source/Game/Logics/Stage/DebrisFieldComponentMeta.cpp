// DebrisFieldComponent のシリアライズ/インスペクタ定義。

#include "Game/Logics/Stage/DebrisFieldComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// DebrisFieldComponent
//   kinds / rocks は実行時に DebrisFieldSystem が作るので、Fields() に載せない。
// ===========================================================================
COMPONENT_META_BEGIN_FULL(DebrisFieldComponent, "Debris Field", CCL::Categories::Effect, 190, false)
CFIELD_T(prefabFolder, "プレハブのフォルダ", "生成",
         "ここにある岩のプレハブを混ぜて使う。例: Assets/Prefabs/Stage/Props")
CFIELD_INT_RANGED(count, "数", 0, 20000, "生成")
CFIELD(seed, "シード", "生成")

CFIELD_FLOAT_RANGED(radiusMin, "半径(最小)", 1.0f, 0.0f, 2000.0f, "帯の形")
CFIELD_FLOAT_RANGED(radiusMax, "半径(最大)", 1.0f, 0.0f, 2000.0f, "帯の形")
CFIELD_FLOAT_RANGED(heightMin, "高さ(最小)", 0.5f, -500.0f, 500.0f, "帯の形")
CFIELD_FLOAT_RANGED(heightMax, "高さ(最大)", 0.5f, -500.0f, 500.0f, "帯の形")

CFIELD_FLOAT_RANGED(orbitSpeed, "公転(度/秒)", 0.05f, -90.0f, 90.0f, "動き")
CFIELD_FLOAT_RANGED(speedJitter, "公転のばらつき", 0.01f, 0.0f, 1.0f, "動き")
CFIELD_FLOAT_RANGED(spinSpeedMax, "自転の最高速(度/秒)", 0.5f, 0.0f, 360.0f, "動き")
CFIELD_FLOAT_RANGED(bobAmplitude, "上下の揺れ幅", 0.05f, 0.0f, 50.0f, "動き")
CFIELD_FLOAT_RANGED(bobFrequency, "上下の揺れ(Hz)", 0.01f, 0.0f, 5.0f, "動き")

CFIELD_FLOAT_RANGED(scaleMin, "大きさ(最小)", 0.01f, 0.0f, 20.0f, "見た目")
CFIELD_FLOAT_RANGED(scaleMax, "大きさ(最大)", 0.01f, 0.0f, 20.0f, "見た目")

CFIELD_RT(elapsed, "経過時間", "状態")
COMPONENT_META_END(DebrisFieldComponent)
