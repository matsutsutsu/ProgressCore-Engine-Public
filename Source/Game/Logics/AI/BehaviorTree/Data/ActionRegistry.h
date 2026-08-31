/// @file  ActionRegistry.h
/// @brief BTの語彙（行動・条件）の定義。ID・保存キー・表示名・分類をここで持つ。
///
/// 【この書き方を選んだ理由】
///   定義表をマクロ（X-macro）で持つと追加が1箇所で済むが、代償として
///   F12（定義へ移動）が表の行に着かない・IntelliSense が弱る・エラー位置が
///   表の行を指さない、という日常的なコストを常時払うことになる。
///
///   そこで「1行が何箇所に展開されるか」で使い分けている。
///     条件 / 行動 / 分類 … 1行につき2箇所（定数と辞書）。手で2行書けば済むので素直に書く
///     ドローン陣形技     … 1行につき4箇所（定数・辞書・陣形への変換・BTへの登録）。
///                          しかも DroneFormationType との対応を保つ必要があり、
///                          ずれてもコンパイルが通ってしまう。ここだけマクロにする
///
///   つまり「重複が痛い所にだけ道具を使い、痛くない所は読みやすさを取る」。
///   全部マクロにするのは、読む回数の多い側を犠牲にして書く回数の少ない側を守る取引になる。
///
/// 【アセットに保存されるのは数値IDではなくキー文字列】
///   例 "A_DroneGuillotineSlam"。数値IDは実行時だけの都合。
///     なぜそうしたか:
///       以前はBTアセットが生の数値（2023 など）を持っていた。IDを詰め直せず、
///       JSONを開いても何の技か読めず、複数人で技を足すと同じ番号を取り合う。
///       名前を正にするとこの3つが同時に消える。AnimGraph が paramName/paramHash で
///       既に採っている形（名前が正・数値は派生）に合わせた。
///     キーに定数名そのもの（#Name / 手書きなら同じ綴り）を使う理由:
///       grep でコードとアセットを直接突き合わせられる。
///       重複はビルド時の検査（下部の static_assert）で落とす。
///
/// 【数値IDの位置づけ ── 通し番号であって、意味は持たない】
///   100番台/1000番台/2000番台 という区切りは「次の空き番号を探しやすくする」ための
///   割り当て台帳であって、分類ではない。分類は category 列だけが持つ。
///   実際 A_DroneStrike は 1000番台にありながら分類は Drone で、桁と分類は一致しない。
///   桁から分類を導く処理はコード上に1つも無い。
///   IDは追加のみ。詰め直すと旧アセットの数値フォールバックが別の技を指す。
#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Game/Logics/AI/Drone/DroneComponent.h" // DroneFormationType

// ============================================================================
// ドローン陣形技の定義表 ── ここだけマクロ
// ============================================================================
/// @brief 列は (定数名, ID, 対応する陣形, 表示名)。
///
/// この1行から4つが作られる:
///   ① CCL::AI::A_Drone〜 の定数
///   ② g_ActionRegistry のエントリ（キー・表示名・分類）
///   ③ TryGetDroneFormation の変換（ActionID → DroneFormationType）
///   ④ BTActions_Boss.h の DroneAction 登録
///
/// 陣形を列に持たせている理由:
///   以前は ActionID = 2000 + (陣形の値 - 1) という算術で変換していた。
///   2つの enum が数式だけで結ばれていたので、片方を触ってもコンパイルは通り、
///   実行時に黙って別の技が出た。対応を1行に並べれば目で確認できる。
///
/// ここに載せてよいのは FormationRegistry に IFormation 実装がある技だけ。
///   構想だけの技を置くと、候補には出るのに実行すると静止する状態になる。
///
/// @note 行を足すときは、行末のバックスラッシュを他の行と縦に揃えること
///       （最終行だけ付けない）。ここが唯一、手で桁を合わせる必要がある場所。
#define BT_DRONE_ACTION_LIST(X)                                                                                \
    X(A_DroneOrbit,              2000, OrbitCircle,             "ドローン：降下円陣 (Orbit Circle)")           \
    X(A_DroneHighOrbit,          2004, HighOrbit,               "ドローン：頭上円陣 (High Orbit)")             \
    X(A_DroneKamikazeSwarm,      2016, KamikazeSwarm,           "ドローン：弾幕 (Kamikaze Swarm)")             \
    X(A_DroneSideSlapWhip,       2021, SideSlapWhip,            "ドローン：横なぎ回転 (Side Slap Whip)")       \
    X(A_DroneGuillotineSlam,     2023, GuillotineSlam,          "ドローン：縦割り (Guillotine Slam)")          \
    X(A_DroneClapSlam,           2024, ClapSlam,                "ドローン：挟み込み1段 (Clap Slam)")           \
    X(A_DroneTsunamiWave,        2025, TsunamiWave,             "ドローン：波状縦割り・左→右 (Wave)")          \
    X(A_DroneMegaClapUpperFirst, 2026, MegaClapSlam_UpperFirst, "ドローン：挟み込み2段・上段先攻 (Mega Clap)") \
    X(A_DroneMegaClapLowerFirst, 2027, MegaClapSlam_LowerFirst, "ドローン：挟み込み2段・下段先攻 (Mega Clap)") \
    X(A_DroneTsunamiWaveRL,      2030, TsunamiWave_RL,          "ドローン：波状縦割り・右→左 (Wave)")          \
    X(A_DroneSquadAssault,       2031, SquadAssault,            "ドローン：小隊突撃 (Squad Assault)")

// ============================================================================
// ID定義
// ============================================================================
namespace CCL::AI
{
// --- 条件 (1〜99) ---
inline constexpr ActionID C_RangeClose              = 1; ///< 距離 < 15m
inline constexpr ActionID C_RangeMedium             = 2; ///< 距離 15〜40m
inline constexpr ActionID C_IsPhase2                = 3; ///< フェーズ >= 2
inline constexpr ActionID C_ShouldTransitionPhase2  = 4; ///< 第1形態 かつ HP <= 閾値
inline constexpr ActionID C_RangeMostClose          = 5; ///< 距離 < 8m
inline constexpr ActionID C_DamageThresholdExceeded = 6; ///< 蓄積ダメージ >= 閾値
inline constexpr ActionID C_RangeFar                = 7; ///< 距離 > 40m
inline constexpr ActionID C_IsAtStageEdge           = 8; ///< 原点から48m以上

// --- 行動 ---
/// @note 100〜111 は「待機0.5秒」〜「待機10.0秒」だった欠番。待ち時間は param へ移したので、
///       A_Wait 1つに畳んである（旧IDは BehaviorTreeLoader が読み替える）。
inline constexpr ActionID A_Wait        = 112;  ///< 待つ。秒数は param
inline constexpr ActionID A_Move        = 1000; ///< プレイヤーへ移動。BossActionSystem が実行
inline constexpr ActionID A_ResetDamage = 1005; ///< ブラックボードの蓄積ダメージをリセット
inline constexpr ActionID A_DroneStrike = 1006; ///< ドローンへ「今撃て」信号（陣形攻撃のリリース）
inline constexpr ActionID A_BossSlash   = 1101; ///< 薙ぎ払い。硬直は param（未設定なら既定0.8s）

// --- ドローン陣形技 (2000番台) ---
#define CCL_BT_DEFINE_DRONE_ID(Name, Id, Formation, Display) inline constexpr ActionID Name = Id;
BT_DRONE_ACTION_LIST(CCL_BT_DEFINE_DRONE_ID)
#undef CCL_BT_DEFINE_DRONE_ID

} // namespace CCL::AI

// ============================================================================
// 分類
// ============================================================================
/// @brief 行動・条件の分類。エディタのコンボの見出しに使う。
/// @note  Common/Boss/Drone は行動用、Range/State は条件用。
///        ActionEntry を行動と条件で共用しているので enum も1つにまとめている。
enum class BTCategory : uint8_t
{
    None,
    Common, ///< どのAIでも使う汎用
    Boss,   ///< ボス本体の行動
    Drone,  ///< ドローン群への指示
    Range,  ///< 条件：距離
    State,  ///< 条件：状態
};

inline const char *CategoryName(BTCategory c)
{
    switch (c) {
    case BTCategory::None:   return "なし";
    case BTCategory::Common: return "汎用";
    case BTCategory::Boss:   return "ボス本体";
    case BTCategory::Drone:  return "ドローン";
    case BTCategory::Range:  return "条件：距離";
    case BTCategory::State:  return "条件：状態";
    }
    return "?";
}

// ============================================================================
// 辞書（エディタ表示とアセットの名前解決に共用）
// ============================================================================
struct ActionEntry
{
    ActionID    id;
    const char *key;        ///< アセットに保存される名前。定数名と同じ綴りにする
    const char *name;       ///< エディタ表示名
    BTCategory  category;   ///< 分類。数値IDの桁ではなくこれが唯一の情報源
    const char *paramLabel; ///< param の意味。空文字なら param を使わない
};

/// @note 並びは分類ごとにまとめる。エディタのコンボが「分類が変わった所で見出しを打つ」
///       作りなので、散らばると同じ見出しが何度も出る（下部の static_assert が見張る）。
inline constexpr ActionEntry g_ActionRegistry[] = {
    { 0,                     "None",          "なし (None)",                  BTCategory::None,   ""              },

    { CCL::AI::A_Wait,        "A_Wait",        "待機",                        BTCategory::Common, "待ち時間 [秒]" },

    { CCL::AI::A_Move,        "A_Move",        "ボス：移動 (Move To Player)", BTCategory::Boss,   ""              },
    { CCL::AI::A_ResetDamage, "A_ResetDamage", "ボス：蓄積ダメージリセット",  BTCategory::Boss,   ""              },
    { CCL::AI::A_BossSlash,   "A_BossSlash",   "ボス：薙ぎ払い (Slash)",      BTCategory::Boss,   "硬直 [秒]"     },

    { CCL::AI::A_DroneStrike, "A_DroneStrike", "ドローン：攻撃実行 (Strike)", BTCategory::Drone,  ""              },

    // 陣形技はマクロから生成する（定数・変換・BT登録と同じ1行が出どころ）
#define CCL_BT_DRONE_ENTRY(Name, Id, Formation, Display) \
    { CCL::AI::Name, #Name, Display, BTCategory::Drone, "" },
    BT_DRONE_ACTION_LIST(CCL_BT_DRONE_ENTRY)
#undef CCL_BT_DRONE_ENTRY
};
inline constexpr int g_ActionRegistryCount = sizeof(g_ActionRegistry) / sizeof(ActionEntry);

inline constexpr ActionEntry g_ConditionRegistry[] = {
    { 0,                                 "None",                      "なし (None)",                             BTCategory::None,  "" },

    { CCL::AI::C_RangeClose,              "C_RangeClose",              "距離 < 15m",                             BTCategory::Range, "" },
    { CCL::AI::C_RangeMedium,             "C_RangeMedium",             "距離 15-40m",                            BTCategory::Range, "" },
    { CCL::AI::C_RangeMostClose,          "C_RangeMostClose",          "距離 < 8m",                              BTCategory::Range, "" },
    { CCL::AI::C_RangeFar,                "C_RangeFar",                "距離 > 40m",                             BTCategory::Range, "" },

    { CCL::AI::C_IsPhase2,                "C_IsPhase2",                "フェーズ >= 2",                          BTCategory::State, "" },
    { CCL::AI::C_ShouldTransitionPhase2,  "C_ShouldTransitionPhase2",  "第2形態移行条件 (HP半分以下 & 第1形態)", BTCategory::State, "" },
    { CCL::AI::C_DamageThresholdExceeded, "C_DamageThresholdExceeded", "ダメージ蓄積 > 閾値",                    BTCategory::State, "" },
    { CCL::AI::C_IsAtStageEdge,           "C_IsAtStageEdge",           "ステージの端にいる",                     BTCategory::State, "" },
};
inline constexpr int g_ConditionRegistryCount = sizeof(g_ConditionRegistry) / sizeof(ActionEntry);

// ============================================================================
// 保存キーの読み替え（リネーム対応）
// ============================================================================
/// @brief 旧キー→新キー。定数名を変えたら、ここに1行足す。
///
/// 何のためにあるか:
///   保存キーは定数名と同じ綴りなので、C++側で改名するとアセットが指す先を失う。
///   この表があれば、アセットを1つも開き直さずに改名できる。
///   Unreal の ActiveClassRedirects と同じ役割。
///
/// 使い終わったら消してよいか:
///   全アセットをエディタで開いて保存し直せば新しいキーで書き直されるので消せる。
///   ただしリポジトリ外のバックアップや作業中ブランチには古い木が残るため、
///   消すのは「もう古い木は存在しない」と言い切れるときだけにする。
///
/// @note 綴り間違い・改名し忘れは下部の static_assert が落とす。
inline constexpr std::string_view RedirectKey(std::string_view key)
{
    // 例: if (key == "A_BossSlash") return "A_BossSweep";
    return key;
}

// ============================================================================
// 引き当て
// ============================================================================
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

/// @brief 保存キーからIDを引く。改名済みのキーも読み替えて解決する。
/// @note  アセット読み込み時に1回だけ走るので線形探索で足りる。
///        実行時の評価はIDのまま行うので、ここは速度要件に乗らない。
inline bool TryFindActionIdByKey(std::string_view key, ActionID &outId)
{
    key = RedirectKey(key);
    for (int i = 0; i < g_ActionRegistryCount; ++i)
        if (key == g_ActionRegistry[i].key) { outId = g_ActionRegistry[i].id; return true; }
    return false;
}

inline bool TryFindConditionIdByKey(std::string_view key, ActionID &outId)
{
    key = RedirectKey(key);
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
        if (key == g_ConditionRegistry[i].key) { outId = g_ConditionRegistry[i].id; return true; }
    return false;
}

// ============================================================================
// 検査
//   表が育ってから壊れると原因を追えないので、すべてビルドで止める。
//   手書きの表になったぶん、ここの検査が食い違いを防ぐ主役になる。
// ============================================================================
/// @brief IDとキーが表の中で一意か。
inline constexpr bool IsRegistryUnique(const ActionEntry *table, int count)
{
    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j) {
            if (table[i].id == table[j].id) return false;
            if (std::string_view(table[i].key) == table[j].key) return false;
        }
    return true;
}
static_assert(IsRegistryUnique(g_ActionRegistry, g_ActionRegistryCount),
              "ActionRegistry の ID かキーが重複している");
static_assert(IsRegistryUnique(g_ConditionRegistry, g_ConditionRegistryCount),
              "ConditionRegistry の ID かキーが重複している");

/// @brief 分類ごとにまとまって並んでいるか。
/// @note  エディタのコンボは「分類が変わった所で見出しを打つ」作りなので、
///        散らばっていると同じ見出しが何度も出る。並べ替えの事故を防ぐ。
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

/// @brief そのキーが現行の表に存在するか。
inline constexpr bool RegistryHasKey(std::string_view key)
{
    for (int i = 0; i < g_ActionRegistryCount; ++i)
        if (key == g_ActionRegistry[i].key) return true;
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
        if (key == g_ConditionRegistry[i].key) return true;
    return false;
}

/// @brief 読み替えが健全か。
///   読み替え先が存在しない … 綴り間違い。読み替えても解決できず無意味になる
///   読み替え元がまだ現役   … 改名し忘れ。現行キーが読み替えで潰される
/// @note  RedirectKey に1行足したら、ここにも対応する1行を足す。
inline constexpr bool IsRedirectValid(std::string_view oldKey, std::string_view newKey)
{
    return RegistryHasKey(newKey) && !RegistryHasKey(oldKey) && RedirectKey(oldKey) == newKey;
}
// 例: static_assert(IsRedirectValid("A_BossSlash", "A_BossSweep"), "読み替えが不正");

/// @brief ドローン技のキーは陣形技として引けるか（マクロ生成部と手書き部の突き合わせ）。
inline constexpr bool DroneEntriesArePresent()
{
#define CCL_BT_DRONE_KEY_CHECK(Name, Id, Formation, Display) if (!RegistryHasKey(#Name)) return false;
    BT_DRONE_ACTION_LIST(CCL_BT_DRONE_KEY_CHECK)
#undef CCL_BT_DRONE_KEY_CHECK
    return true;
}
static_assert(DroneEntriesArePresent(), "ドローン技が辞書に載っていない");

// ============================================================================
// 種別の判定
//   桁の範囲ではなく、表そのものを引く。桁で判定すると、番号の付け方を変えた
//   瞬間に静かに壊れる。
// ============================================================================
/// @brief ドローン技のActionIDから、作らせる陣形を引く。技でなければ false。
/// @note  DroneAction（BTActions_Boss.h）と BTウィンドウが共有する唯一の変換口。
inline bool TryGetDroneFormation(ActionID id, DroneFormationType &outFormation)
{
    switch (id) {
#define CCL_BT_DRONE_CASE(Name, Id, Formation, Display) \
    case Id: outFormation = DroneFormationType::Formation; return true;
        BT_DRONE_ACTION_LIST(CCL_BT_DRONE_CASE)
#undef CCL_BT_DRONE_CASE
    default: return false;
    }
}

inline bool IsDroneAction(ActionID id)
{
    DroneFormationType ignored;
    return TryGetDroneFormation(id, ignored);
}

inline bool IsWaitAction(ActionID id) { return id == CCL::AI::A_Wait; }

inline bool IsBossPhysicalAction(ActionID id) { return id == CCL::AI::A_Move; }

/// @brief ボスの近接アクション（アニメトリガーで攻撃する系）か。
///        今後 踏み込み/突進 を足したらここに条件を追加する。
inline bool IsBossMeleeAction(ActionID id) { return id == CCL::AI::A_BossSlash; }

/// @brief この行動が使う param の意味。使わないなら nullptr。
/// @note  エディタが入力欄を出すかの判定に使う。
inline const char *GetActionParamLabel(ActionID id)
{
    const ActionEntry *e = FindActionEntry(id);
    return (e && e->paramLabel && e->paramLabel[0]) ? e->paramLabel : nullptr;
}
