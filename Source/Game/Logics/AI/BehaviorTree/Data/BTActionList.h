#pragma once
// ===========================================================================
// 概要: BT の行動・条件・ドローンの陣形技を並べた表（X マクロ）。
//
// 責務:
//   - 行動の表（BT_ACTION_LIST）、条件の表（BT_CONDITION_LIST）、
//     C++ で書いた陣形の表（BT_DRONE_ACTION_LIST）を定義する
//   - 陣形技の役割（DroneActionRole）を定義する
//
// 備考:
//   技・条件を足すときに編集するのはこのファイルだけ。ID・辞書・エディタの候補・検査は
//   すべてこの表から作られる（BTActionIds.h）。
//
//   決まり:
//     1. ID は同じ表の中で一意ならよい。条件と行動は別の表なので番号が重なってよい
//        （ノードは自分が行動か条件かを type で持ち、引く表が決まっているため）
//     2. Drone 分類の行動は 100 番以上（BTActionIds.h の static_assert で検査する）
//     3. 同じ Category は続けて並べる（エディタのコンボが分類の変わり目で見出しを出すため）
//     4. アセットの保存キーは Name からそのまま作られる。Name を変えたら RedirectKey に1行足す
//     5. ID を振り直すときはアセット内の数値も一緒に振り直す
//        （読み込みはキーの文字列を優先するので壊れはしないが、古い数値が残ると読むときに混乱する）
//
//   表から作られるもの:
//     表 ──┬─ ID 定数
//          ├─ 辞書（表示名・分類・param・振る舞い）
//          ├─ 陣形への変換（陣形技のみ）
//          └─ static_assert（重複・分類の連続性・Drone の区画）
// ===========================================================================
#include <cstdint>

// ============================================================
// 行動
// ============================================================
//
// Kind  Instant  即 Success
//       Blocking 時間を消費するが、体へは何も要求しない
//       Driving  時間を消費し、currentActionId を毎フレーム書く（Parallel では主に置く）
// Param 空文字ならエディタに入力欄を出さない
//
//    Name              ID   Category Kind      Param            Display
// ---------------------------------------------------------------------
#define BT_ACTION_LIST(X) \
    /* Common */                                                                                           \
    X(A_Wait,           1,   Common,  Blocking, "待ち時間 [秒]", "待機")                                   \
                                                                                                           \
    /* Boss */                                                                                             \
    X(A_Move,           2,   Boss,    Driving,  "",              "ボス：移動 (Move To Player)")            \
    X(A_ResetDamage,    3,   Boss,    Instant,  "",              "ボス：蓄積ダメージリセット")             \
    X(A_BossBackStep,   4,   Boss,    Driving,  "全体時間 [秒]", "ボス：後方ステップ (Back Step)")         \
    X(A_BossLeapEscape, 7,   Boss,    Driving,  "全体時間 [秒]", "ボス：中央へ跳んで逃げる (Leap Escape)") \
    X(A_BossLeapAttack, 8,   Boss,    Driving,  "全体時間 [秒]", "ボス：跳躍攻撃 (Leap Attack)")           \
    X(A_BossAirHover,   9,   Boss,    Driving,  "全体時間 [秒]", "ボス：その場で滞空 (Air Hover)")         \
    X(A_BossDashAttack, 10,  Boss,    Driving,  "全体時間 [秒]", "ボス：突進攻撃 (Dash Attack)")           \
    X(A_BossStrafe,     11,  Boss,    Driving,  "全体時間 [秒]", "ボス：横歩きで様子見 (Strafe)")          \
                                                                                                           \
    /* Drone */                                                                                            \
    X(A_DroneFormation, 128, Drone,   Instant,  "技の枠番号",    "ドローン：データの技")                    \
    X(A_DroneStrike,    100, Drone,   Instant,  "",              "ドローン：攻撃実行 (Strike)")

// ============================================================
// 条件
// ============================================================
//
// 条件は評価して真偽を返すだけなので Kind を持たない。
//
//    Name                       ID  Category Param                     Display
// ----------------------------------------------------------------------------
#define BT_CONDITION_LIST(X) \
    /* Range */                                                                                                   \
    X(C_DistanceLessThan,        1,  Range,   "距離 [m]",               "距離が N m 未満")                        \
    X(C_DistanceGreaterThan,     2,  Range,   "距離 [m]",               "距離が N m 超")                          \
                                                                                                                  \
    /* Spatial */                                                                                                 \
    X(C_TargetAngleLessThan,     3,  Spatial, "角度 [度]",              "相手が正面 ±N 度の内側")                 \
    X(C_TargetAngleGreaterThan,  4,  Spatial, "角度 [度]",              "相手が正面 ±N 度の外側（回り込み）")     \
    X(C_TargetApproaching,       5,  Spatial, "速さ [m/s]",             "相手が N m/s 以上で詰めてきている")      \
    X(C_TargetRetreating,        6,  Spatial, "速さ [m/s]",             "相手が N m/s 以上で離れている")          \
    X(C_IsAtStageEdge,           7,  Spatial, "端からの距離 [m]",       "ステージの端から N m 以内")              \
    X(C_TargetOnCenterSide,      8,  Spatial, "角度 [度]",              "相手が中央側 ±N 度にいる（追い込まれ）") \
    X(C_Cornered,                13, Spatial, "端からの距離 [m]",       "追い込まれ（端から N m 以内・相手が中央側）") \
                                                                                                                  \
    /* State */                                                                                                   \
    X(C_IsPhase2,                9,  State,   "",                       "フェーズ >= 2")                          \
    X(C_ShouldTransitionPhase2,  10, State,   "",                       "第2形態移行条件 (HP半分以下 & 第1形態)") \
    X(C_DamageThresholdExceeded, 11, State,   "最大HPに対する割合 [%]", "被弾の蓄積 >= 最大HPの N %")         \
    X(C_RecentlyHit,             12, State,   "経過時間 [秒]",          "直近の被弾から N 秒以内")

// ============================================================
// ドローンの陣形
// ============================================================
//
// ここに並ぶのは C++ クラスで書いた陣形だけ。データ（Assets/Formations/*.json）で書いた技は
// 行を持たず、A_DroneFormation に枠番号を渡して指す。技を1つ作るたびにこの表を編集して
// ビルドし直すのでは、エディタで技を組める利点がほとんど無くなるため。
//
// Role      Attack   当たり判定を持つ技
//           Standby  隊形。攻撃しない
// Formation DroneFormationType の識別子。陣形の実装が無い枠を指すと静止する
//
// 分類も振る舞いも全部同じ（指示を出して即 Success）なので、その2列は持たない。
//
// 役割を行に持たせているのは、攻撃しない隊形を足すたびに別の場所の除外リストを
// 直さなくて済むようにするため。

// 陣形技の役割。表の Role 列がこの値の名前を書く。
enum class DroneActionRole : uint8_t
{
    Attack,  // 当たり判定を持つ技
    Standby, // 隊形。攻撃しない
};

//    Name                       ID   Formation                Role     Display
// ----------------------------------------------------------------------------
#define BT_DRONE_ACTION_LIST(X)                                                            \
    /* Standby */                                                                          \
    X(A_DroneLowOrbit,      101, LowOrbit,     Standby, "ドローン：低空円陣 (Low Orbit)")  \
    X(A_DroneHighOrbit,     102, HighOrbit,    Standby, "ドローン：頭上円陣 (High Orbit)") \
                                                                                           \
    /* Attack */                                                                           \
    X(A_DroneBarrage,       103, Barrage,      Attack,  "ドローン：弾幕 (Barrage)")        \
    X(A_DroneSquadAssault,  111, SquadAssault, Attack,  "ドローン：小隊突撃 (Squad Assault)")

