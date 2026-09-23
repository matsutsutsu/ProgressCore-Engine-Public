//=============================================================================
// Behavior Tree Vocabulary
//=============================================================================
//
// [EDIT]
//   技・条件を足すとき編集するのはこのファイルだけ。ID・辞書・エディタの候補・検査は
//   すべてこの表から生成される（ActionRegistry.h）。
//
// [RULE]
//   1. ID は同じ表の中で一意ならよい。条件と行動は別の表なので番号が重なってよい
//   2. Drone 分類の行動は 100 番以上（ActionRegistry.h の static_assert が守る）
//   3. 同じ Category は連続して並べる
//   4. アセットの保存キーは Name からそのまま作られる。Name を変えたら RedirectKey に1行足す
//   5. ID を振り直すときはアセット内の数値も一緒に振り直す
//
// [WHY]
//   1. ノードは自分が行動か条件かを type で持っていて、引く表が最初から決まっている
//   3. エディタのコンボが「分類が変わった所で見出しを打つ」作り
//   5. 読み込みはキー文字列が正なので壊れはしないが、古い数値が残ると読むときに混乱する
//
// [IMPL]
//   表 ──┬─ ID定数
//        ├─ 辞書（表示名・分類・param・振る舞い）
//        ├─ 陣形への変換（陣形技のみ）
//        └─ static_assert（重複・分類の連続性・Drone の区画）
//=============================================================================
#pragma once
#include <cstdint>

//=============================================================================
// Actions
//=============================================================================
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
    X(A_BossHitWait,    5,   Boss,    Blocking, "待つ長さ [秒]", "ボス：被弾後の様子見")                   \
    X(A_BossAdvance,    6,   Boss,    Driving,  "長さ [秒]",     "ボス：詰めながら待つ")                   \
    X(A_BossLeapEscape, 7,   Boss,    Driving,  "全体時間 [秒]", "ボス：中央へ跳んで逃げる (Leap Escape)") \
    X(A_BossLeapAttack, 8,   Boss,    Driving,  "全体時間 [秒]", "ボス：跳躍攻撃 (Leap Attack)")           \
    X(A_BossAirHover,   9,   Boss,    Driving,  "全体時間 [秒]", "ボス：その場で滞空 (Air Hover)")         \
                                                                                                           \
    /* Drone */                                                                                            \
    X(A_DroneStrike,    100, Drone,   Instant,  "",              "ドローン：攻撃実行 (Strike)")

//=============================================================================
// Conditions
//=============================================================================
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
                                                                                                                  \
    /* State */                                                                                                   \
    X(C_IsPhase2,                9,  State,   "",                       "フェーズ >= 2")                          \
    X(C_ShouldTransitionPhase2,  10, State,   "",                       "第2形態移行条件 (HP半分以下 & 第1形態)") \
    X(C_DamageThresholdExceeded, 11, State,   "最大HPに対する割合 [%]", "被弾の蓄積 >= 最大HPの N %")

//=============================================================================
// Drone Formation Actions
//=============================================================================
//
// Role      Attack   当たり判定を持つ技
//           Standby  隊形。攻撃しない
// Formation DroneFormationType の識別子。陣形の実装が無い枠を指すと静止する
//
// 分類も振る舞いも全部同じ（指示を出して即 Success）なので、その2列は持たない。
//
// [WHY] 役割を行に持たせているのは、以前「円陣と頭上円陣だけを除く」という手書きの
//       除外リストで判定していて、攻撃しない隊形を足すたびに別の場所を直す必要があったため。

// 陣形技の役割。表の Role 列がこの値の名前を書く。
enum class DroneActionRole : uint8_t
{
    Attack,  // 当たり判定を持つ技
    Standby, // 隊形。攻撃しない
};

//    Name                       ID   Formation                Role     Display
// ----------------------------------------------------------------------------
#define BT_DRONE_ACTION_LIST(X) \
    /* Standby */                                                                                                      \
    X(A_DroneOrbit,              101, OrbitCircle,             Standby, "ドローン：降下円陣 (Orbit Circle)")           \
    X(A_DroneHighOrbit,          102, HighOrbit,               Standby, "ドローン：頭上円陣 (High Orbit)")             \
                                                                                                                       \
    /* Attack */                                                                                                       \
    X(A_DroneKamikazeSwarm,      103, KamikazeSwarm,           Attack,  "ドローン：弾幕 (Kamikaze Swarm)")             \
    X(A_DroneSideSlapWhip,       104, SideSlapWhip,            Attack,  "ドローン：横なぎ回転 (Side Slap Whip)")       \
    X(A_DroneGuillotineSlam,     105, GuillotineSlam,          Attack,  "ドローン：縦割り (Guillotine Slam)")          \
    X(A_DroneClapSlam,           106, ClapSlam,                Attack,  "ドローン：挟み込み1段 (Clap Slam)")           \
    X(A_DroneTsunamiWave,        107, TsunamiWave,             Attack,  "ドローン：波状縦割り・左→右 (Wave)")          \
    X(A_DroneMegaClapUpperFirst, 108, MegaClapSlam_UpperFirst, Attack,  "ドローン：挟み込み2段・上段先攻 (Mega Clap)") \
    X(A_DroneMegaClapLowerFirst, 109, MegaClapSlam_LowerFirst, Attack,  "ドローン：挟み込み2段・下段先攻 (Mega Clap)") \
    X(A_DroneTsunamiWaveRL,      110, TsunamiWave_RL,          Attack,  "ドローン：波状縦割り・右→左 (Wave)")          \
    X(A_DroneSquadAssault,       111, SquadAssault,            Attack,  "ドローン：小隊突撃 (Squad Assault)")          \
                                                                                                                       \
    /* Editor Data Slots */                                                                                            \
    X(A_DroneDataSlot00,         112, DataSlot00,              Attack,  "ドローン：[データ] 枠0")                      \
    X(A_DroneDataSlot01,         113, DataSlot01,              Attack,  "ドローン：[データ] 枠1")                      \
    X(A_DroneDataSlot02,         114, DataSlot02,              Attack,  "ドローン：[データ] 枠2")                      \
    X(A_DroneDataSlot03,         115, DataSlot03,              Attack,  "ドローン：[データ] 枠3")                      \
    X(A_DroneDataSlot04,         116, DataSlot04,              Attack,  "ドローン：[データ] 枠4")                      \
    X(A_DroneDataSlot05,         117, DataSlot05,              Attack,  "ドローン：[データ] 枠5")                      \
    X(A_DroneDataSlot06,         118, DataSlot06,              Attack,  "ドローン：[データ] 枠6")                      \
    X(A_DroneDataSlot07,         119, DataSlot07,              Attack,  "ドローン：[データ] 枠7")                      \
    X(A_DroneDataSlot08,         120, DataSlot08,              Attack,  "ドローン：[データ] 枠8")                      \
    X(A_DroneDataSlot09,         121, DataSlot09,              Attack,  "ドローン：[データ] 枠9")                      \
    X(A_DroneDataSlot10,         122, DataSlot10,              Attack,  "ドローン：[データ] 枠10")                     \
    X(A_DroneDataSlot11,         123, DataSlot11,              Attack,  "ドローン：[データ] 枠11")                     \
    X(A_DroneDataSlot12,         124, DataSlot12,              Attack,  "ドローン：[データ] 枠12")                     \
    X(A_DroneDataSlot13,         125, DataSlot13,              Attack,  "ドローン：[データ] 枠13")                     \
    X(A_DroneDataSlot14,         126, DataSlot14,              Attack,  "ドローン：[データ] 枠14")                     \
    X(A_DroneDataSlot15,         127, DataSlot15,              Attack,  "ドローン：[データ] 枠15")
