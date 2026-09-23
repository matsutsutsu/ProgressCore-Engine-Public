// BTの語彙（行動・条件）の定義。ID・保存キー・表示名・分類をここで持つ。
//
// アセットに保存されるのは数値IDではなくキー文字列（例 "A_DroneGuillotineSlam"）。
// 定数名そのものを綴りに使うので、grep でコードとアセットを突き合わせられる。
//
// 数値IDは通し番号であって意味を持たない。100/1000/2000番台は空き番号を探すための
// 台帳で、分類は category 列だけが持つ。IDは追加のみ（詰め直すと旧アセットの
// 数値フォールバックが別の技を指す）。
//
// マクロは「1行が4箇所へ展開され、ずれてもコンパイルが通る」陣形技だけに使う。
// 行動・条件は2箇所なので、F12 が効くよう手で書く。
#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Game/Logics/AI/Drone/DroneComponent.h" // DroneFormationType

// ドローン陣形技の定義表 (定数名, ID, 対応する陣形, 表示名)。
// この1行から、定数・辞書エントリ・陣形への変換・BTへの登録の4つが作られる。
//
// 陣形を列に持つのは、以前 ActionID = 2000 + (陣形の値 - 1) という算術で変換していて、
// 片方を触ってもコンパイルが通り実行時に黙って別の技が出たため。
// 載せてよいのは FormationRegistry に IFormation 実装がある技だけ
// （構想だけの技を置くと、候補には出るのに実行すると静止する）。
//
// データ枠(2032〜)だけはこの規則の例外で、対応する JSON が無ければ実装も無い。
// それでも先に載せているのは、陣形を1つ作るたびにこの表を編集してリビルドするなら
// エディタで組む意味が半分消えるため。空き枠を選んだときに気づけるよう、
// 起動時に「どの枠が埋まったか」をログへ出している（Formations_DataDriven）。
#define BT_DRONE_ACTION_LIST(X)                                                                                \
    X(A_DroneOrbit, 2000, OrbitCircle, "ドローン：降下円陣 (Orbit Circle)")                                    \
    X(A_DroneHighOrbit, 2004, HighOrbit, "ドローン：頭上円陣 (High Orbit)")                                    \
    X(A_DroneKamikazeSwarm, 2016, KamikazeSwarm, "ドローン：弾幕 (Kamikaze Swarm)")                            \
    X(A_DroneSideSlapWhip, 2021, SideSlapWhip, "ドローン：横なぎ回転 (Side Slap Whip)")                        \
    X(A_DroneGuillotineSlam, 2023, GuillotineSlam, "ドローン：縦割り (Guillotine Slam)")                       \
    X(A_DroneClapSlam, 2024, ClapSlam, "ドローン：挟み込み1段 (Clap Slam)")                                    \
    X(A_DroneTsunamiWave, 2025, TsunamiWave, "ドローン：波状縦割り・左→右 (Wave)")                             \
    X(A_DroneMegaClapUpperFirst, 2026, MegaClapSlam_UpperFirst, "ドローン：挟み込み2段・上段先攻 (Mega Clap)") \
    X(A_DroneMegaClapLowerFirst, 2027, MegaClapSlam_LowerFirst, "ドローン：挟み込み2段・下段先攻 (Mega Clap)") \
    X(A_DroneTsunamiWaveRL, 2030, TsunamiWave_RL, "ドローン：波状縦割り・右→左 (Wave)")                        \
    X(A_DroneSquadAssault, 2031, SquadAssault, "ドローン：小隊突撃 (Squad Assault)")                           \
    X(A_DroneDataSlot00, 2032, DataSlot00, "ドローン：[データ] 枠0")                                           \
    X(A_DroneDataSlot01, 2033, DataSlot01, "ドローン：[データ] 枠1")                                           \
    X(A_DroneDataSlot02, 2034, DataSlot02, "ドローン：[データ] 枠2")                                           \
    X(A_DroneDataSlot03, 2035, DataSlot03, "ドローン：[データ] 枠3")                                           \
    X(A_DroneDataSlot04, 2036, DataSlot04, "ドローン：[データ] 枠4")                                           \
    X(A_DroneDataSlot05, 2037, DataSlot05, "ドローン：[データ] 枠5")                                           \
    X(A_DroneDataSlot06, 2038, DataSlot06, "ドローン：[データ] 枠6")                                           \
    X(A_DroneDataSlot07, 2039, DataSlot07, "ドローン：[データ] 枠7")                                           \
    X(A_DroneDataSlot08, 2040, DataSlot08, "ドローン：[データ] 枠8")                                           \
    X(A_DroneDataSlot09, 2041, DataSlot09, "ドローン：[データ] 枠9")                                           \
    X(A_DroneDataSlot10, 2042, DataSlot10, "ドローン：[データ] 枠10")                                          \
    X(A_DroneDataSlot11, 2043, DataSlot11, "ドローン：[データ] 枠11")                                          \
    X(A_DroneDataSlot12, 2044, DataSlot12, "ドローン：[データ] 枠12")                                          \
    X(A_DroneDataSlot13, 2045, DataSlot13, "ドローン：[データ] 枠13")                                          \
    X(A_DroneDataSlot14, 2046, DataSlot14, "ドローン：[データ] 枠14")                                          \
    X(A_DroneDataSlot15, 2047, DataSlot15, "ドローン：[データ] 枠15")

namespace CCL::AI
{
    // --- 条件 (1〜99) ---
    // 1/2/5/7 は間合いをIDへ焼き込んでいた頃の欠番。IDで表すと敵の種類×間合いの数だけ
    // IDが増えるので、距離は param へ移して2つの条件に統合した。
    inline constexpr ActionID C_IsPhase2                = 3;  // フェーズ >= 2
    inline constexpr ActionID C_ShouldTransitionPhase2  = 4;  // 第1形態 かつ HP <= 閾値
    inline constexpr ActionID C_DamageThresholdExceeded = 6;  // 蓄積被弾 >= 最大HPの param %
    inline constexpr ActionID C_IsAtStageEdge           = 8;  // ステージの端から param m 以内
    inline constexpr ActionID C_DistanceLessThan        = 9;  // ターゲットとの距離 < param
    inline constexpr ActionID C_DistanceGreaterThan     = 10; // ターゲットとの距離 > param
    // 位置関係。距離だけだと「正面8m」と「背後8m」が同じ状況になり、回り込みにも
    // 踏み込みにも反応できない。角度・接近速度も param で持つ（距離と同じ理由）。
    inline constexpr ActionID C_TargetAngleLessThan    = 11; // 相手が正面 ±param 度の内側
    inline constexpr ActionID C_TargetAngleGreaterThan = 12; // 相手が正面 ±param 度の外側
    inline constexpr ActionID C_TargetApproaching      = 13; // 相手が param m/s 以上で接近
    inline constexpr ActionID C_TargetRetreating       = 14; // 相手が param m/s 以上で後退
    // 追い込まれの判定は「端に居る」とこれを Sequence で並べて作る。1つの条件にまとめると
    // param が1つしか持てず、端からの距離か角度のどちらかが即値に固まる。
    inline constexpr ActionID C_TargetOnCenterSide = 15; // 相手が中央の方向 ±param 度にいる

    // --- 行動 ---
    // 100〜111 は「待機0.5秒」〜「待機10.0秒」だった欠番。param へ移して1つに畳んである。
    inline constexpr ActionID A_Wait        = 112;  // 待つ。秒数は param
    inline constexpr ActionID A_Move        = 1000; // プレイヤーへ移動。BossActionSystem が実行
    inline constexpr ActionID A_ResetDamage = 1005; // ブラックボードの蓄積ダメージをリセット
    inline constexpr ActionID A_DroneStrike = 1006; // ドローンへ「今撃て」信号（陣形攻撃のリリース）
    // 1100番台は「アニメのトリガーを引き、param 秒だけBTを留める」同じ形。
    // 違いは物理挙動だけで、それは BossActionSystem が ActionID ごとに分岐して担当する。
    // 1101〜1103 は剣の技（薙ぎ払い・突進斬り・跳躍斬り）を削除した欠番。再利用すると、
    // 数値IDで残った古いノードが別の技として動き出す。
    inline constexpr ActionID A_BossBackStep = 1104; // 後方ステップ。被弾がかさんだら間合いを切る

    // 被弾はモーションを中断せず、止めるのは「次の技を選ぶ」判断だけ。その猶予を作る。
    inline constexpr ActionID A_BossHitWait = 1105; // 被弾後の様子見。param 秒だけ次の技を選ばない

    // 技と技の間に入れて、待機時間を「棒立ち」から「圧をかける時間」へ変える
    // （A_Move は「遠いから追う」行動で、こちらは待つことが目的）。
    inline constexpr ActionID A_BossAdvance = 1106; // 詰めながら待つ。param 秒

    // 跳躍系。跳び方は共通で、違いは着地点（BossLeapLanding）だけ。
    inline constexpr ActionID A_BossLeapEscape = 1107; // 中央を挟んで相手の反対側へ跳ぶ
    inline constexpr ActionID A_BossLeapAttack = 1108; // 相手の位置へ跳び、着地の衝撃で当てる
    inline constexpr ActionID A_BossAirHover  = 1109; // その場へ跳び上がり、頂点に留まる（滞空）

// --- ドローン陣形技 (2000番台) ---
#define CCL_BT_DEFINE_DRONE_ID(Name, Id, Formation, Display) inline constexpr ActionID Name = Id;
    BT_DRONE_ACTION_LIST(CCL_BT_DEFINE_DRONE_ID)
#undef CCL_BT_DEFINE_DRONE_ID

} // namespace CCL::AI

// 行動・条件の分類。エディタのコンボの見出しに使う。
// ActionEntry を行動と条件で共用しているので enum も1つにまとめている。
enum class BTCategory : uint8_t
{
    None,
    Common,  // どのAIでも使う汎用
    Boss,    // ボス本体の行動
    Drone,   // ドローン群への指示
    Range,   // 条件：距離
    Spatial, // 条件：位置関係（角度・接近速度）
    State,   // 条件：状態
};

inline const char *CategoryName(BTCategory c)
{
    switch (c)
    {
    case BTCategory::None:
        return "なし";
    case BTCategory::Common:
        return "汎用";
    case BTCategory::Boss:
        return "ボス本体";
    case BTCategory::Drone:
        return "ドローン";
    case BTCategory::Range:
        return "条件：距離";
    case BTCategory::Spatial:
        return "条件：位置関係";
    case BTCategory::State:
        return "条件：状態";
    }
    return "?";
}

// 行動がBTに対してどう振る舞うか。読み込み時の検査（BehaviorTreeLoader）が引く。
//
// 表の列にしてあるのは、以前これを表の外の if 連鎖2本で持っていて、技を1つ足すたびに
// 両方を手で直す必要があったため。片方を忘れても当然コンパイルは通り、Parallel の
// 置き間違いを見逃す検査が黙って出来上がる。
//
// Driving は Blocking を兼ねる（要求を毎フレーム書く技は必ず時間がかかる）ので、
// 2つのフラグではなく1列3値で持つ。
enum class BTActionKind : uint8_t
{
    Instant,  // 即 Success。陣形指示・攻撃信号・リセット
    Blocking, // Running を返しうる（時間がかかる）が、体へは何も要求しない。待機系
    Driving,  // 時間がかかり、体の要求(currentActionId)も毎フレーム書く。ボス本体の技
};

// 辞書（エディタ表示とアセットの名前解決に共用）。
struct ActionEntry
{
    ActionID     id;
    const char  *key;                          // アセットに保存される名前。定数名と同じ綴りにする
    const char  *name;                         // エディタ表示名
    BTCategory   category;                     // 分類。数値IDの桁ではなくこれが唯一の情報源
    const char  *paramLabel;                   // param の意味。空文字なら param を使わない
    BTActionKind kind = BTActionKind::Instant; // BTに対する振る舞い。条件の表では使わない
};

// 並びは分類ごとにまとめる。エディタのコンボが「分類が変わった所で見出しを打つ」
// 作りなので、散らばると同じ見出しが何度も出る（下部の static_assert が見張る）。
inline constexpr ActionEntry g_ActionRegistry[] = {
    {0, "None", "なし (None)", BTCategory::None, ""},

    {CCL::AI::A_Wait, "A_Wait", "待機", BTCategory::Common, "待ち時間 [秒]", BTActionKind::Blocking},

    {CCL::AI::A_Move, "A_Move", "ボス：移動 (Move To Player)", BTCategory::Boss, "", BTActionKind::Driving},
    {CCL::AI::A_ResetDamage, "A_ResetDamage", "ボス：蓄積ダメージリセット", BTCategory::Boss, ""},
    {CCL::AI::A_BossBackStep, "A_BossBackStep", "ボス：後方ステップ (Back Step)", BTCategory::Boss, "全体時間 [秒]", BTActionKind::Driving},
    {CCL::AI::A_BossHitWait, "A_BossHitWait", "ボス：被弾後の様子見", BTCategory::Boss, "待つ長さ [秒]", BTActionKind::Blocking},
    {CCL::AI::A_BossAdvance, "A_BossAdvance", "ボス：詰めながら待つ", BTCategory::Boss, "長さ [秒]", BTActionKind::Driving},
    {CCL::AI::A_BossLeapEscape, "A_BossLeapEscape", "ボス：中央へ跳んで逃げる (Leap Escape)", BTCategory::Boss, "全体時間 [秒]", BTActionKind::Driving},
    {CCL::AI::A_BossLeapAttack, "A_BossLeapAttack", "ボス：跳躍攻撃 (Leap Attack)", BTCategory::Boss, "全体時間 [秒]", BTActionKind::Driving},
    {CCL::AI::A_BossAirHover, "A_BossAirHover", "ボス：その場で滞空 (Air Hover)", BTCategory::Boss, "全体時間 [秒]", BTActionKind::Driving},

    {CCL::AI::A_DroneStrike, "A_DroneStrike", "ドローン：攻撃実行 (Strike)", BTCategory::Drone, ""},

#define CCL_BT_DRONE_ENTRY(Name, Id, Formation, Display) \
    {CCL::AI::Name, #Name, Display, BTCategory::Drone, ""},
    BT_DRONE_ACTION_LIST(CCL_BT_DRONE_ENTRY)
#undef CCL_BT_DRONE_ENTRY
};
inline constexpr int g_ActionRegistryCount = sizeof(g_ActionRegistry) / sizeof(ActionEntry);

inline constexpr ActionEntry g_ConditionRegistry[] = {
    {0, "None", "なし (None)", BTCategory::None, ""},

    {CCL::AI::C_DistanceLessThan, "C_DistanceLessThan", "距離が N m 未満", BTCategory::Range, "距離 [m]"},
    {CCL::AI::C_DistanceGreaterThan, "C_DistanceGreaterThan", "距離が N m 超", BTCategory::Range, "距離 [m]"},

    {CCL::AI::C_TargetAngleLessThan, "C_TargetAngleLessThan", "相手が正面 ±N 度の内側", BTCategory::Spatial, "角度 [度]"},
    {CCL::AI::C_TargetAngleGreaterThan, "C_TargetAngleGreaterThan", "相手が正面 ±N 度の外側（回り込み）", BTCategory::Spatial, "角度 [度]"},
    {CCL::AI::C_TargetApproaching, "C_TargetApproaching", "相手が N m/s 以上で詰めてきている", BTCategory::Spatial, "速さ [m/s]"},
    {CCL::AI::C_TargetRetreating, "C_TargetRetreating", "相手が N m/s 以上で離れている", BTCategory::Spatial, "速さ [m/s]"},
    {CCL::AI::C_IsAtStageEdge, "C_IsAtStageEdge", "ステージの端から N m 以内", BTCategory::Spatial, "端からの距離 [m]"},
    {CCL::AI::C_TargetOnCenterSide, "C_TargetOnCenterSide", "相手が中央側 ±N 度にいる（追い込まれ）", BTCategory::Spatial, "角度 [度]"},

    {CCL::AI::C_IsPhase2, "C_IsPhase2", "フェーズ >= 2", BTCategory::State, ""},
    {CCL::AI::C_ShouldTransitionPhase2, "C_ShouldTransitionPhase2", "第2形態移行条件 (HP半分以下 & 第1形態)", BTCategory::State, ""},
    {CCL::AI::C_DamageThresholdExceeded, "C_DamageThresholdExceeded", "被弾の蓄積 >= 最大HPの N %", BTCategory::State, "最大HPに対する割合 [%]"},
};
inline constexpr int g_ConditionRegistryCount = sizeof(g_ConditionRegistry) / sizeof(ActionEntry);

// 保存キーの読み替え（リネーム対応）。定数名を変えたら、ここに1行足す。
// 保存キーは定数名と同じ綴りなので、これが無いとC++側の改名でアセットが指す先を失う。
// Unreal の ActiveClassRedirects と同じ役割。綴り間違いは下部の static_assert が落とす。
inline constexpr std::string_view RedirectKey(std::string_view key)
{
    // 例: if (key == "A_BossJumpSlam") return "A_BossLeapAttack";
    return key;
}

inline const ActionEntry *FindActionEntry(ActionID id)
{
    for (int i = 0; i < g_ActionRegistryCount; ++i)
        if (g_ActionRegistry[i].id == id) return &g_ActionRegistry[i];
    return nullptr;
}

inline const ActionEntry *FindConditionEntry(ActionID id)
{
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
        if (g_ConditionRegistry[i].id == id) return &g_ConditionRegistry[i];
    return nullptr;
}

// 保存キーからIDを引く。改名済みのキーも読み替えて解決する。
// アセット読み込み時に1回だけ走るので線形探索で足りる（実行時の評価はIDのまま）。
inline bool TryFindActionIdByKey(std::string_view key, ActionID &outId)
{
    key = RedirectKey(key);
    for (int i = 0; i < g_ActionRegistryCount; ++i)
        if (key == g_ActionRegistry[i].key)
        {
            outId = g_ActionRegistry[i].id;
            return true;
        }
    return false;
}

inline bool TryFindConditionIdByKey(std::string_view key, ActionID &outId)
{
    key = RedirectKey(key);
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
        if (key == g_ConditionRegistry[i].key)
        {
            outId = g_ConditionRegistry[i].id;
            return true;
        }
    return false;
}

// --- 検査。手書きの表なので、食い違いはすべてビルドで止める ---

// IDとキーが表の中で一意か。
inline constexpr bool IsRegistryUnique(const ActionEntry *table, int count)
{
    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
        {
            if (table[i].id == table[j].id) return false;
            if (std::string_view(table[i].key) == table[j].key) return false;
        }
    return true;
}
static_assert(IsRegistryUnique(g_ActionRegistry, g_ActionRegistryCount),
              "ActionRegistry の ID かキーが重複している");
static_assert(IsRegistryUnique(g_ConditionRegistry, g_ConditionRegistryCount),
              "ConditionRegistry の ID かキーが重複している");

// 分類ごとにまとまって並んでいるか。エディタのコンボは「分類が変わった所で見出しを
// 打つ」作りなので、散らばっていると同じ見出しが何度も出る。
inline constexpr bool IsGroupedByCategory(const ActionEntry *table, int count)
{
    for (int i = 1; i < count; ++i)
        if (table[i].category != table[i - 1].category)
            for (int j = 0; j < i - 1; ++j)
                if (table[j].category == table[i].category) return false;
    return true;
}
static_assert(IsGroupedByCategory(g_ActionRegistry, g_ActionRegistryCount),
              "ActionRegistry の分類が飛び飛びになっている（表の並びを分類順に直すこと）");
static_assert(IsGroupedByCategory(g_ConditionRegistry, g_ConditionRegistryCount),
              "ConditionRegistry の分類が飛び飛びになっている（表の並びを分類順に直すこと）");

// そのキーが現行の表に存在するか。
inline constexpr bool RegistryHasKey(std::string_view key)
{
    for (int i = 0; i < g_ActionRegistryCount; ++i)
        if (key == g_ActionRegistry[i].key) return true;
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
        if (key == g_ConditionRegistry[i].key) return true;
    return false;
}

// 読み替えが健全か（先が無ければ綴り間違い、元がまだ現役なら改名し忘れ）。
// RedirectKey に1行足したら、ここにも対応する1行を足す。
inline constexpr bool IsRedirectValid(std::string_view oldKey, std::string_view newKey)
{
    return RegistryHasKey(newKey) && !RegistryHasKey(oldKey) && RedirectKey(oldKey) == newKey;
}
// 例: static_assert(IsRedirectValid("A_BossJumpSlam", "A_BossLeapAttack"), "読み替えが不正");

// ドローン技のキーは陣形技として引けるか（マクロ生成部と手書き部の突き合わせ）。
inline constexpr bool DroneEntriesArePresent()
{
#define CCL_BT_DRONE_KEY_CHECK(Name, Id, Formation, Display) \
    if (!RegistryHasKey(#Name)) return false;
    BT_DRONE_ACTION_LIST(CCL_BT_DRONE_KEY_CHECK)
#undef CCL_BT_DRONE_KEY_CHECK
    return true;
}
static_assert(DroneEntriesArePresent(), "ドローン技が辞書に載っていない");

// --- 種別の判定。桁の範囲ではなく表そのものを引く（桁で判定すると番号の付け方を
//     変えた瞬間に静かに壊れる）---

// ドローン技のActionIDから、作らせる陣形を引く。技でなければ false。
inline bool TryGetDroneFormation(ActionID id, DroneFormationType &outFormation)
{
    switch (id)
    {
#define CCL_BT_DRONE_CASE(Name, Id, Formation, Display) \
    case Id:                                            \
        outFormation = DroneFormationType::Formation;   \
        return true;
        BT_DRONE_ACTION_LIST(CCL_BT_DRONE_CASE)
#undef CCL_BT_DRONE_CASE
    default:
        return false;
    }
}

inline bool IsDroneAction(ActionID id)
{
    DroneFormationType ignored;
    return TryGetDroneFormation(id, ignored);
}

// 当たり判定を持つ陣形（技）か。隊形である円陣・頭上円陣だけを除く。
inline bool IsDroneAttackFormation(ActionID id)
{
    return IsDroneAction(id) && id != CCL::AI::A_DroneOrbit && id != CCL::AI::A_DroneHighOrbit;
}

inline bool IsWaitAction(ActionID id) { return id == CCL::AI::A_Wait; }

inline bool IsBossPhysicalAction(ActionID id)
{
    return id == CCL::AI::A_Move || id == CCL::AI::A_BossAdvance;
}

// この行動は BossCommandComponent::currentActionId を書くか。
//
//   これらは「毎フレーム評価される」ことを前提に2つの後始末をしている。
//     ・currentActionId を毎フレーム書き直す
//     ・硬直が終わったフレームに ClearCommandIfMine で要求を下ろす
//   BossActionSystem は currentActionId を毎フレーム読んで体を動かすので、
//   評価が止まると技が終わっても体がその技を続ける。
//   Parallel の「従」に置いてはいけない行動の判定に使う。
inline bool IsCommandWritingAction(ActionID id)
{
    const ActionEntry *e = FindActionEntry(id);
    return e && e->kind == BTActionKind::Driving;
}

// この行動は Running を返しうるか。陣形指示・突撃信号・リセットはどれも即 Success で、
// これらだけで枝を作ると Selector がそこで打ち切り、下の枝が一度も評価されない
// （外からは「固まって何もしない」に見えるので読み込み時に警告する）。
inline bool IsBlockingAction(ActionID id)
{
    const ActionEntry *e = FindActionEntry(id);
    return e && e->kind != BTActionKind::Instant;
}

// この行動が使う param の意味。使わないなら nullptr（エディタの入力欄の有無）。
inline const char *GetActionParamLabel(ActionID id)
{
    const ActionEntry *e = FindActionEntry(id);
    return (e && e->paramLabel && e->paramLabel[0]) ? e->paramLabel : nullptr;
}

// この条件が使う param の意味。使わないなら nullptr。
inline const char *GetConditionParamLabel(ActionID id)
{
    const ActionEntry *e = FindConditionEntry(id);
    return (e && e->paramLabel && e->paramLabel[0]) ? e->paramLabel : nullptr;
}
