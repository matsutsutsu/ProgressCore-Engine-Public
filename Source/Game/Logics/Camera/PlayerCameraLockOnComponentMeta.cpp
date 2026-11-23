#include "Game/Logics/Camera/PlayerCameraLockOnComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(PlayerCameraLockOnComponent, "Player Camera Lock On", CCL::Categories::Camera, 351, false)

// --- 狙う敵 ---
CFIELD_RT_T(target, "ロックオン中の敵", "狙う敵",
            "ロックオン時に注視する敵のEntity。\n"
            "通常はゲーム側のロックオンシステムが設定します。")
CFIELD_RT_T(isActive, "ロックオン有効化", "狙う敵",
            "ロックオンを有効にします。\n"
            "通常はゲーム側が自動的にON/OFFします。")

// --- 見上げ角 ---
CFIELD_FLOAT_RANGED_T(pitch, "ロックオン中のピッチ", 0.5f, -60.0f, 60.0f, "見上げ角",
                      "ロックオン中に保つカメラの上下角度。\n"
                      "値を下げるほどカメラが高くなり、上から見下ろす視点になります。\n"
                      "値を上げるほどカメラが低くなり、下から見上げる視点になります。\n"
                      "上空のドローンを見たいときは正の値にします。")
CFIELD_FLOAT_RANGED_T(pitchDamping, "ロックオン中のピッチ補間", 0.1f, 0.0f, 50.0f, "見上げ角",
                      "現在のピッチから「ロックオン中のピッチ」へ寄る速さ。\n"
                      "大きいほど素早く切り替わり、小さいほどゆっくり切り替わります。\n"
                      "0にするとほぼ切り替わらなくなります。")

// --- 構図 ---
CFIELD_FLOAT_RANGED_T(subjugationRatio, "敵への注視偏重率", 0.01f, 0.0f, 0.5f, "構図",
                      "プレイヤーと敵の間のどこを見るかを決めます。\n"
                      "0 = プレイヤー中心。\n"
                      "値を大きくするほど敵側へ注視点が移動します。\n"
                      "大きすぎるとプレイヤーが画面端へ寄りやすくなります。")
CFIELD_FLOAT_RANGED_T(maxLookShift, "注視ずれ上限", 0.1f, 0.0f, 15.0f, "構図",
                      "ロックオン時にプレイヤーから敵側へずらせる注視点の最大距離(m)。\n"
                      "大きいほど敵を中心にしやすくなります。\n"
                      "小さいほどプレイヤーを画面内に残しやすくなります。")
CFIELD_FLOAT_RANGED_T(distanceGain, "離間による引き量", 0.01f, 0.0f, 2.0f, "構図",
                      "プレイヤーと敵の距離に応じてカメラを後ろへ引く量。\n"
                      "大きいほど敵が遠くなったときカメラも大きく引きます。\n"
                      "0にすると距離による追加の引きはありません。")
CFIELD_FLOAT_RANGED_T(maxExtraDistance, "引き量の上限", 0.1f, 0.0f, 30.0f, "構図",
                      "ロックオンによって追加で引けるカメラ距離の最大値(m)。\n"
                      "大きいほど遠距離の敵まで画面に収めやすくなります。")

// --- ヨーの自動補正 ---
CFIELD_T(allowManualCamera, "ロックオン中の手動操作", "ヨーの自動補正",
         "ON = ロックオン中でも右スティックやマウスでカメラを操作できます。\n"
         "OFF = ロックオン中はカメラ操作を制限します。")
CFIELD_FLOAT_RANGED_T(yawDeadZone, "Yawデッドゾーン", 0.5f, 0.0f, 180.0f, "ヨーの自動補正",
                      "敵が画面中央からずれていても自動補正しない角度範囲。\n"
                      "大きいほど自動補正が発生しにくくなります。\n"
                      "小さいほど敵を中央へ戻そうとします。")
CFIELD_FLOAT_RANGED_T(assistStrength, "自動補正の強さ", 0.1f, 0.0f, 20.0f, "ヨーの自動補正",
                      "ロックオン中に敵方向へカメラを戻す強さ。\n"
                      "大きいほど敵へ素早く向きます。\n"
                      "大きすぎるとカメラが忙しく動く場合があります。")
CFIELD_FLOAT_RANGED_T(maxCorrectionSpeed, "自動補正最大速度", 1.0f, 0.0f, 500.0f, "ヨーの自動補正",
                      "ロックオン自動補正でカメラが1秒間に回転できる最大速度(度/秒)。\n"
                      "大きいほど敵へ素早く向きます。\n"
                      "小さいほどゆっくり自然に補正します。")
CFIELD_FLOAT_RANGED_T(manualOverrideTime, "手動操作後の補正停止時間", 0.01f, 0.0f, 5.0f, "ヨーの自動補正",
                      "プレイヤーがカメラを操作した後、自動補正を停止する時間(秒)。\n"
                      "大きいほどプレイヤー操作を優先します。\n"
                      "小さいほどすぐに敵への自動補正が再開します。")

COMPONENT_META_END(PlayerCameraLockOnComponent)
