// TrailComponent のシリアライズ/インスペクタ定義。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include <json.hpp>
using json = nlohmann::json;

// ===========================================================================
// TrailComponent
//   リングバッファ（segments/head/count）は Fields() に載せないので、保存も編集もしない。
// ===========================================================================
COMPONENT_META_BEGIN_FULL(TrailComponent, "Weapon Trail", CCL::Categories::Effect, 180, false)
CFIELD_ENUM_U8(shape, "断面の形", TrailShapeNames, TrailShapeCount, "形状")
CFIELD_FLOAT3_FAST(baseOffset, "根本オフセット", 0.01f, "形状")
CFIELD_FLOAT3_FAST(tipOffset, "先端オフセット", 0.01f, "形状")
CFIELD_FLOAT_RANGED(width, "基本幅", 0.01f, 0.0f, 3.0f, "形状")
CFIELD_FLOAT_RANGED(widthCurvePower, "幅カーブ", 0.1f, 0.1f, 5.0f, "形状")
CFIELD_FLOAT_RANGED(thickness, "太さ(0=根本〜先端の半分)", 0.01f, 0.0f, 10.0f, "形状")
CFIELD_INT_RANGED(tubeSides, "筒の分割数", 3, 16, "形状")
CFIELD_FLOAT_RANGED(rimPower, "筒の輪郭強調", 0.05f, 0.0f, 1.0f, "見た目")

CFIELD_COLOR(color, "色(加算)", "見た目")
CFIELD_ASSET(texturePath, "テクスチャ", "テクスチャ", "Texture",
             "トレイルに貼る画像([...]で選択)。"
             "空なら単色。U=長さ方向/V=幅方向。αで輪郭、RGBで模様、U方向グラデで寿命色変化。")
CFIELD_T(uvTiling, "UVタイリング(長さ,幅)", "テクスチャ", "テクスチャの繰り返し回数。x=長さ方向, y=幅方向。")
CFIELD_T(uvScroll, "UVスクロール速度(長さ,幅)", "テクスチャ", "毎秒のUV移動量。流れる/揺らぐ表現に。0で静止。")
CFIELD_FLOAT_RANGED(lifeTime, "寿命(秒)", 0.005f, 0.0f, 2.0f, "見た目")
CFIELD_FLOAT_RANGED(minDistance, "最小距離", 0.005f, 0.0f, 1.0f, "形状")

CFIELD_FLOAT_RANGED(distortion, "歪み量(px)", 0.1f, 0.0f, 64.0f, "歪み")
CFIELD_FLOAT_RANGED(distortionChroma, "歪みの色ズレ", 0.01f, 0.0f, 1.0f, "歪み")
CFIELD_FLOAT_RANGED(distortionNoise, "陽炎ゆらぎ", 0.01f, 0.0f, 1.0f, "歪み")

CFIELD_FLOAT_RANGED(burstDuration, "ダッシュ時のTrail出現時間", 0.1f, 0.0f, 10.0f, "時間")
CFIELD_RO(burstTimer, "一時Trail表示の残り時間", "時間", nullptr)
CFIELD_RO(timeScale, "老化の時間倍率", "時間", nullptr)

CFIELD(emitting, "発生中", "状態")
CFIELD_T(animControlled, "アニメ制御", "状態",
         "ONで Trail アニメイベントの区間中だけ発生。OFFは手動(Emitting)運用。")
CFIELD(useInterpolation, "Catmull-Rom補間", "状態")
COMPONENT_META_END(TrailComponent)
