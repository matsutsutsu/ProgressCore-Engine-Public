// MenuStyleComponent のシリアライズ/インスペクタ定義。
#include "Game/UI/Common/MenuStyleComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(MenuStyleComponent, "Menu Style (メニュー共通の見た目)", CCL::Categories::UI, 64, false)
CFIELD_FLOAT_RANGED(left, "左端(px)", 0.5f, 0.0f, 800.0f, "見出しと行")
CFIELD_FLOAT_RANGED(headY, "見出しの上端(px)", 0.5f, 0.0f, 600.0f, "見出しと行")
CFIELD_FLOAT_RANGED(headPx, "見出しの文字の高さ(px)", 0.5f, 8.0f, 200.0f, "見出しと行")
CFIELD_FLOAT_RANGED(ruleGap, "見出しから線まで(px)", 0.5f, 0.0f, 200.0f, "見出しと行")
CFIELD_FLOAT_RANGED(ruleW, "線の長さ(px)", 1.0f, 0.0f, 2000.0f, "見出しと行")
CFIELD_FLOAT_RANGED(subGap, "線から下の1行まで(px)", 0.5f, 0.0f, 200.0f, "見出しと行")
CFIELD_FLOAT_RANGED(rowGap, "下の1行から記録まで(px)", 0.5f, 0.0f, 300.0f, "見出しと行")
CFIELD_FLOAT_RANGED(rowH, "記録の行の間隔(px)", 0.5f, 8.0f, 200.0f, "見出しと行")
CFIELD_FLOAT_RANGED(rowPx, "記録の文字の高さ(px)", 0.5f, 8.0f, 96.0f, "見出しと行")
CFIELD_FLOAT_RANGED(valueOffset, "左端から値の列まで(px)", 1.0f, 0.0f, 1000.0f, "見出しと行")
CFIELD_FLOAT_RANGED(typeSpeed, "打ち込む速さ(文字/秒)", 1.0f, 1.0f, 500.0f, "見出しと行")
CFIELD_COLOR(gold, "白金（飾り・勝ち）", "色")
CFIELD_COLOR(red, "深紅（ボス・負け）", "色")
CFIELD_COLOR(label, "記録の項目名", "色")
CFIELD_COLOR(value, "記録の値", "色")
CFIELD_COLOR(sub, "線の下の1行", "色")
CFIELD(buttonSize, "ボタンの大きさ(px)", "ボタン")
CFIELD_FLOAT_RANGED(labelPad, "ラベルの左の余白(px)", 0.5f, 0.0f, 200.0f, "ボタン")
CFIELD_FLOAT_RANGED(labelPx, "ラベルの文字の高さ(px)", 0.5f, 8.0f, 96.0f, "ボタン")
CFIELD_COLOR(buttonBg, "板の色", "ボタン")
CFIELD_COLOR(buttonFill, "選んだときの帯の色", "ボタン")
CFIELD_COLOR(buttonGlow, "帯の先端の光", "ボタン")
CFIELD_FLOAT_RANGED(buttonSlant, "帯の傾き(px)", 0.5f, 0.0f, 64.0f, "ボタン")
CFIELD_FLOAT_RANGED(buttonSheen, "光沢の強さ", 0.01f, 0.0f, 1.0f, "ボタン")
CFIELD_COLOR(labelIdle, "ラベル（選んでいない）", "ボタン")
CFIELD_COLOR(labelFocus, "ラベル（選んでいる）", "ボタン")
CFIELD_FLOAT_RANGED(focusSpeed, "帯の伸びる速さ", 0.1f, 0.1f, 60.0f, "ボタン")
CFIELD_FLOAT_RANGED(pressTime, "押した演出の長さ(秒)", 0.01f, 0.01f, 2.0f, "ボタン")
CFIELD_FLOAT_RANGED(hintIconPx, "アイコンの大きさ(px)", 0.5f, 8.0f, 128.0f, "操作のヒント")
CFIELD_FLOAT_RANGED(hintTextPx, "文字の高さ(px)", 0.5f, 8.0f, 64.0f, "操作のヒント")
CFIELD_COLOR(hintColor, "文字の色", "操作のヒント")
COMPONENT_META_END(MenuStyleComponent)
