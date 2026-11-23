#include "Game/Logics/Camera/PlayerCameraComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(PlayerCameraComponent, "Player Camera", CCL::Categories::Camera, 350, false)

// --- 追従・注視ターゲット ---
CFIELD_T(target, "追従ターゲット", "追従・注視ターゲット",
         "カメラが追従する対象。\n"
         "通常はプレイヤーのEntityを指定します。")

// --- 位置・追従 ---
CFIELD_FLOAT3_FAST_T(targetOffset, "注視点オフセット", 0.1f, "位置・追従",
                     "プレイヤーの基準位置から注視点をずらします。\n"
                     "Yを上げると頭・肩付近を注視します。\n"
                     "Yを下げると胸・足元寄りになります。")
CFIELD_FLOAT_RANGED_T(distance, "カメラ距離", 0.1f, 1.0f, 100.0f, "位置・追従",
                      "通常時のプレイヤーからカメラまでの距離(m)。\n"
                      "大きいほどカメラが後ろへ下がります。\n"
                      "小さいほどキャラクターへ近づきます。")

// --- 視野角・レンズ ---
CFIELD_FLOAT_RANGED_T(baseFov, "基本視野角", 0.1f, 10.0f, 120.0f, "視野角・レンズ",
                      "通常時の視野角(FOV)。\n"
                      "大きいほど広い範囲が映り、画面の遠近感が強くなります。\n"
                      "小さいほど望遠レンズのようになります。\n"
                      "ダッシュ中の広がりは Player Dash Component の「ダッシュ中の画角の足し込み」で決めます。")

// --- 回転・オービット ---
CFIELD_FLOAT_RANGED_T(currentPitch, "初期ピッチ角", 0.5f, -85.0f, 85.0f, "回転・オービット",
                      "カメラの初期上下角度。\n"
                      "負の値ほど上から見下ろします。\n"
                      "正の値ほど下から見上げます。")
CFIELD_FLOAT_RANGED_T(currentYaw, "初期ヨー角", 0.5f, -360.0f, 360.0f, "回転・オービット",
                      "カメラの初期左右角度。\n"
                      "プレイヤーに対してどの方向からカメラを開始するかを決めます。")

// --- 回転制限 ---
CFIELD_FLOAT_RANGED_T(minPitch, "最小ピッチ制限", 0.5f, -89.0f, 89.0f, "回転制限",
                      "カメラが上方向へ回り込める限界角度。\n"
                      "より小さくすると高い位置から見下ろせます。")
CFIELD_FLOAT_RANGED_T(maxPitch, "最大ピッチ制限", 0.5f, -89.0f, 89.0f, "回転制限",
                      "カメラが下方向へ回り込める限界角度。\n"
                      "より大きくすると低い位置から見上げられます。")

// --- 操作感度 ---
CFIELD_FLOAT_RANGED_T(lookSpeedX, "回転速度X", 1.0f, 0.0f, 500.0f, "操作感度",
                      "ゲームパッド右スティック左右の回転速度。\n"
                      "大きいほど少しスティックを倒しただけで速く回転します。")
CFIELD_FLOAT_RANGED_T(lookSpeedY, "回転速度Y", 1.0f, 0.0f, 500.0f, "操作感度",
                      "ゲームパッド右スティック上下の回転速度。\n"
                      "大きいほど上下視点が速く動きます。")

// --- マウス視点 ---
CFIELD_T(mouseLookRequiresButton, "右クリック中のみ視点回転", "マウス視点",
         "ON = 右クリック中だけマウスで視点を操作します。\n"
         "OFF = 常にマウスで視点を操作します。")
CFIELD_FLOAT_RANGED_T(mouseSensitivityX, "マウス水平感度", 0.01f, 0.0f, 2.0f, "マウス視点",
                      "マウス左右移動に対するカメラの回転感度。\n"
                      "大きいほど少ないマウス移動で大きく回転します。")
CFIELD_FLOAT_RANGED_T(mouseSensitivityY, "マウス垂直感度", 0.01f, 0.0f, 2.0f, "マウス視点",
                      "マウス上下移動に対するカメラの回転感度。\n"
                      "大きいほど上下視点が速く動きます。")
CFIELD_T(mouseInvertY, "上下反転", "マウス視点",
         "ONにするとマウス上下操作を反転します。")
CFIELD_T(enableCursorLock, "カーソル中央固定", "マウス視点",
         "ONにするとマウスルック中にカーソルを画面中央へ固定します。\n"
         "画面端にカーソルが到達して視点操作が止まるのを防ぎます。")

// --- 滑らかさ ---
CFIELD_FLOAT_RANGED_T(positionDamping, "位置追従遅延", 0.1f, 0.0f, 50.0f, "滑らかさ",
                      "プレイヤーへのカメラ追従の速さ。\n"
                      "大きいほどプレイヤーへ素早く追従します。\n"
                      "小さいほど遅れて追従し、ゴムのような動きになります。")
CFIELD_FLOAT_RANGED_T(fovDamping, "視野角補間速度", 0.1f, 0.0f, 50.0f, "滑らかさ",
                      "目標の視野角（基本視野角＋演出の足し込み）へ寄る速さ。\n"
                      "大きいほど素早く変化します。\n"
                      "小さいほどゆっくり滑らかに変化します。")

// --- 演出の上書き ---
CFIELD_FLOAT_RANGED_T(framingFollowDamping, "上書き中の引く速さ", 0.5f, 1.0f, 100.0f, "演出の上書き",
                      "演出（ラッシュなど）がカメラを引くときの追従の速さ。\n"
                      "距離を動かす演出の間で、地形に当たっていないときだけ使います。\n"
                      "小さいと引きが演出に間に合いません。")
CFIELD_FLOAT_RANGED_T(framingMaxLookShift, "上書きの注視ずれ上限", 0.1f, 0.0f, 15.0f, "演出の上書き",
                      "演出が注視点を敵へ寄せるときの、ずらす量の上限(m)。\n"
                      "大きいほど敵が画面中央に来ますが、プレイヤーが画面外へ出やすくなります。")

// --- カメラ衝突 ---
CFIELD_T(collisionEnabled, "衝突回避", "カメラ衝突",
         "ONにすると壁や地形にカメラがめり込まないようにします。")
CFIELD_FLOAT_RANGED_T(collisionRadius, "衝突半径", 0.01f, 0.0f, 2.0f, "カメラ衝突",
                      "壁からカメラをどれだけ離すかの余白(m)。\n"
                      "大きいほど壁から離れた位置で止まります。")
CFIELD_FLOAT_RANGED_T(collisionMinDistance, "最小距離", 0.05f, 0.1f, 10.0f, "カメラ衝突",
                      "壁に押し込まれた際のカメラとプレイヤーの最小距離(m)。\n"
                      "大きいほどカメラがプレイヤーへ近づきにくくなります。")
CFIELD_FLOAT_RANGED_T(collisionInDamping, "寄せる速さ", 0.5f, 1.0f, 100.0f, "カメラ衝突",
                      "壁にぶつかったときカメラを手前へ寄せる速さ。\n"
                      "大きいほど素早く壁から離れます。")
CFIELD_FLOAT_RANGED_T(collisionOutDamping, "戻す速さ", 0.5f, 1.0f, 100.0f, "カメラ衝突",
                      "壁から離れたときカメラを元の距離へ戻す速さ。\n"
                      "小さいほどゆっくり戻り、大きいほど素早く戻ります。")
CFIELD_FLOAT_RANGED_T(collisionHeadBias, "頭寄せ", 0.01f, 0.0f, 1.0f, "カメラ衝突",
                      "壁に押されてカメラが近づいたときの高さ補正。\n"
                      "0 = 通常の位置。\n"
                      "1 = プレイヤーの注視点の高さへ強く寄せます。\n"
                      "大きいほど足元が映りにくくなります。")

// --- ルックアヘッド ---
CFIELD_FLOAT_RANGED_T(lookaheadMultiplier, "前方注視係数", 0.05f, 0.0f, 5.0f, "ルックアヘッド",
                      "プレイヤーの移動方向へ注視点を先行させる量。\n"
                      "大きいほど進行方向を先に見るようになります。\n"
                      "0にするとルックアヘッドを無効化します。")
CFIELD_FLOAT_RANGED_T(lookaheadDamping, "注視遅延", 0.1f, 0.0f, 50.0f, "ルックアヘッド",
                      "ルックアヘッド位置へ移動する速さ。\n"
                      "大きいほど素早く移動し、小さいほど遅れて追従します。")

COMPONENT_META_END(PlayerCameraComponent)
