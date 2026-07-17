/// @file OverdriveComponentMeta.cpp
/// @brief OverdriveComponent のメタ定義（インスペクタ調整・シリアライズ）。
///        ランタイム状態(currentGauge/isActive/wasDashing)は保存対象外。

#include "OverdriveComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// OverdriveComponent メタデータ定義（Combat カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(OverdriveComponent, "Overdrive", CCL::Categories::Combat, 160, false)
    CFIELD_FLOAT_RANGED(maxGauge,        "最大ゲージ",          1.0f,   1.0f, 1000.0f, "ゲージ")
    CFIELD_FLOAT_RANGED(gainOnJustEvade, "獲得:ジャスト回避",   0.5f,   0.0f, 200.0f,  "ゲージ")
    CFIELD_FLOAT_RANGED(gainOnDash,      "獲得:ダッシュ(救済)",  0.1f,   0.0f, 100.0f,  "ゲージ")
    CFIELD_FLOAT_RANGED(duration,        "継続時間(秒)",       0.1f,   0.5f, 30.0f,   "発動")
    CFIELD_FLOAT_RANGED(enemyTimeScale,       "敵の時間倍率",       0.01f, 0.05f, 1.0f, "発動")
    CFIELD_FLOAT_RANGED(environmentTimeScale, "環境の時間倍率", 0.01f, 0.05f, 1.0f, "発動")
    CFIELD_FLOAT_RANGED(playerTimeScale,      "プレイヤーの時間倍率",      0.01f, 0.5f,  2.0f, "発動")
    CFIELD(currentGauge, "現在ゲージ(実行時)", "状態")
    CFIELD(isActive,     "発動中(実行時)",     "状態")
COMPONENT_META_END(OverdriveComponent)
