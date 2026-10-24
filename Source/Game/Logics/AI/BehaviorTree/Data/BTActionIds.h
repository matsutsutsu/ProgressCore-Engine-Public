#pragma once
// ===========================================================================
// 概要: BTActionList.h の表から、BT の行動・条件の ID 定数と辞書を作るヘッダ。
//
// 責務:
//   - ID 定数、辞書（表示名・分類・param の意味・振る舞い）、陣形への変換を表から作る
//   - ID や保存キーから辞書を引く関数を提供する
//   - 表の重複・分類の並び・ドローンの ID の区画を static_assert で検査する
//
// 備考: 技を足すときにこのファイルを触る必要はない。編集するのは BTActionList.h だけ。
//       引くのは読み込み時とエディタだけなので線形探索でよい（実行中の評価は ID のまま）。
// ===========================================================================
#include <cstdint>
#include <string_view>

#include "Game/Logics/AI/BehaviorTree/Data/BTActionList.h"      // 語彙の表（編集するのはこちら）
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"  // ActionID
#include "Game/Logics/AI/Drone/Components/DroneFormationType.h"            // 技IDから陣形を引くため

// ============================================================
// ID 定数
// ============================================================
//
// 辞書と同じ表から作るので、「ID はあるが辞書に無い」という状態が起きない。

#define CCL_BT_DEFINE_ACTION_ID(Name, Id, Category, Kind, ParamLabel, Display) \
    inline constexpr ActionID Name = Id;
BT_ACTION_LIST(CCL_BT_DEFINE_ACTION_ID)
#undef CCL_BT_DEFINE_ACTION_ID

#define CCL_BT_DEFINE_CONDITION_ID(Name, Id, Category, ParamLabel, Display) \
    inline constexpr ActionID Name = Id;
BT_CONDITION_LIST(CCL_BT_DEFINE_CONDITION_ID)
#undef CCL_BT_DEFINE_CONDITION_ID

#define CCL_BT_DEFINE_DRONE_ID(Name, Id, Formation, Role, Display) \
    inline constexpr ActionID Name = Id;
BT_DRONE_ACTION_LIST(CCL_BT_DEFINE_DRONE_ID)
#undef CCL_BT_DEFINE_DRONE_ID

// ============================================================
// 辞書
// ============================================================

/**
 * @brief 行動・条件の分類。エディタのコンボの見出しになる。
 *
 * ActionEntry を行動と条件で共用しているので enum も1つにまとめている。
 */
enum class BTCategory : uint8_t
{
    None,    // 分類なし（辞書の先頭の None だけが使う）
    Common,  // どのAIでも使う汎用
    Boss,    // ボス本体の行動
    Drone,   // ドローン群への指示
    Range,   // 条件：距離
    Spatial, // 条件：位置関係（角度・接近速度）
    State,   // 条件：状態
};

// 分類の表示名（エディタのコンボの見出し）
inline const char *BTCategoryName(BTCategory c)
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

/**
 * @brief BT上での実行特性。読み込み時の検査（BehaviorTreeLoader）が引く。
 *
 * Driving ⊂ Blocking。Driving は Parallel の主に置く必要がある。
 *
 * 2つのフラグではなく1列3値にしているのは、「Driving だが Blocking でない」という
 * あり得ない組み合わせを書けなくするため。
 */
enum class BTActionKind : uint8_t
{
    Instant,  // 即 Success
    Blocking, // 時間を消費するが、体へは何も要求しない
    Driving,  // 時間を消費し、currentActionId を毎フレーム書く
};

// 辞書の1行（エディタ表示とアセットの名前解決に共用）
struct ActionEntry
{
    ActionID     id;                           // 行動・条件の ID
    const char  *key;                          // アセットに保存される名前。定数名と同じ綴りにする
    const char  *name;                         // エディタ表示名
    BTCategory   category;                     // 分類。数値 ID の桁ではなく、これで分類を判定する
    const char  *paramLabel;                   // param の意味。空文字なら param を使わない
    BTActionKind kind = BTActionKind::Instant; // BTに対する振る舞い。条件の表では使わない
};

// 並びは分類ごとにまとめる。エディタのコンボが「分類が変わった所で見出しを出す」
// 作りなので、散らばると同じ見出しが何度も出る（下の static_assert で検査する）。
inline constexpr ActionEntry g_ActionRegistry[] = {
    {0, "None", "なし (None)", BTCategory::None, ""},

#define CCL_BT_ACTION_ENTRY(Name, Id, Category, Kind, ParamLabel, Display) \
    {Name, #Name, Display, BTCategory::Category, ParamLabel, BTActionKind::Kind},
    BT_ACTION_LIST(CCL_BT_ACTION_ENTRY)
#undef CCL_BT_ACTION_ENTRY

// 陣形技は分類も振る舞いも全部同じ（指示を出して即 Success）なので、表に列を持たない。
#define CCL_BT_DRONE_ENTRY(Name, Id, Formation, Role, Display) \
    {Name, #Name, Display, BTCategory::Drone, "", BTActionKind::Instant},
    BT_DRONE_ACTION_LIST(CCL_BT_DRONE_ENTRY)
#undef CCL_BT_DRONE_ENTRY
};
inline constexpr int g_ActionRegistryCount = sizeof(g_ActionRegistry) / sizeof(ActionEntry);

inline constexpr ActionEntry g_ConditionRegistry[] = {
    {0, "None", "なし (None)", BTCategory::None, ""},

#define CCL_BT_CONDITION_ENTRY(Name, Id, Category, ParamLabel, Display) \
    {Name, #Name, Display, BTCategory::Category, ParamLabel},
    BT_CONDITION_LIST(CCL_BT_CONDITION_ENTRY)
#undef CCL_BT_CONDITION_ENTRY
};
inline constexpr int g_ConditionRegistryCount = sizeof(g_ConditionRegistry) / sizeof(ActionEntry);

// ============================================================
// 引く関数
// ============================================================

/**
 * @brief 保存キーの読み替え（Unreal の ActiveClassRedirects と同じ役割）。定数名を変えたら、ここに1行足す。
 * @note 返してよいのは文字列リテラルか引数の key だけ（string_view は文字列の中身を持たないため）。
 */
inline constexpr std::string_view RedirectKey(std::string_view key)
{
    // 例: if (key == "A_BossJumpSlam") return "A_BossLeapAttack";
    return key;
}

// ID から行動の辞書の行を引く。無ければ nullptr
inline const ActionEntry *FindActionEntry(ActionID id)
{
    for (int i = 0; i < g_ActionRegistryCount; ++i)
        if (g_ActionRegistry[i].id == id) return &g_ActionRegistry[i];
    return nullptr;
}

// ID から条件の辞書の行を引く。無ければ nullptr
inline const ActionEntry *FindConditionEntry(ActionID id)
{
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
        if (g_ConditionRegistry[i].id == id) return &g_ConditionRegistry[i];
    return nullptr;
}

// 保存キーから行動のIDを引く（改名済みのキーも読み替える）。見つからなければ false で outId はそのまま
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

// 保存キーから条件のIDを引く（TryFindActionIdByKey の条件版）
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

// ============================================================
// 検査
// ============================================================

// IDとキーが表の中で一意か
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

// 表が分類ごとにまとまって並んでいるか（理由は g_ActionRegistry の上）
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

// 陣形技（Drone 分類）は100番以上、それ以外は100番未満。
//
// 番号の区画を決まりごとだけにせず、検査で守る。番号から種別を推測したときに
// 必ず当たるようにするため。データの枠を増やしても末尾に伸ばせる。
inline constexpr ActionID kDroneActionIdFirst = 100;

// Drone 分類の行動がすべて kDroneActionIdFirst 以上で、それ以外がすべて未満か
inline constexpr bool DroneIdsAreInBlock()
{
    for (int i = 0; i < g_ActionRegistryCount; ++i)
    {
        const ActionEntry &e = g_ActionRegistry[i];
        if (e.id == 0) continue; // None は区画を持たない

        const bool isDrone   = (e.category == BTCategory::Drone);
        const bool inBlock   = (e.id >= kDroneActionIdFirst);
        if (isDrone != inBlock) return false;
    }
    return true;
}
static_assert(DroneIdsAreInBlock(),
              "ドローン技のIDが100番の区画から外れている（表のID列を直すこと）");

// そのキーが行動か条件の表に存在するか
inline constexpr bool RegistryHasKey(std::string_view key)
{
    for (int i = 0; i < g_ActionRegistryCount; ++i)
        if (key == g_ActionRegistry[i].key) return true;
    for (int i = 0; i < g_ConditionRegistryCount; ++i)
        if (key == g_ConditionRegistry[i].key) return true;
    return false;
}

/**
 * @brief 読み替えが健全かを返す。RedirectKey に1行足したら、ここにも対応する1行を足す。
 * @param oldKey 読み替え元のキー。表に残っていれば改名し忘れ。
 * @param newKey 読み替え先のキー。表に無ければ綴り間違い。
 * @return 先が表にあり、元が表に無く、RedirectKey が元を先へ読み替えるなら true。
 */
inline constexpr bool IsRedirectValid(std::string_view oldKey, std::string_view newKey)
{
    return RegistryHasKey(newKey) && !RegistryHasKey(oldKey) && RedirectKey(oldKey) == newKey;
}
// 例: static_assert(IsRedirectValid("A_BossJumpSlam", "A_BossLeapAttack"), "読み替えが不正");

// ============================================================
// 問い合わせ
// ============================================================
//
// 番号の範囲ではなく表を引く。番号で判定すると、番号の付け方を変えたときに何も言わずに壊れる。

// C++ で書いた陣形技の ID から陣形を引く。それ以外（データの技を含む）は false で outFormation はそのまま
inline bool TryGetDroneFormation(ActionID id, DroneFormationType &outFormation)
{
    switch (id)
    {
#define CCL_BT_DRONE_CASE(Name, Id, Formation, Role, Display) \
    case Id:                                            \
        outFormation = DroneFormationType::Formation;   \
        return true;
        BT_DRONE_ACTION_LIST(CCL_BT_DRONE_CASE)
#undef CCL_BT_DRONE_CASE
    default:
        return false;
    }
}

// ドローンへの指示の行動か（C++ の陣形技とデータの技）
inline bool IsDroneAction(ActionID id)
{
    if (id == A_DroneFormation) return true; // データの技。どの技かは param の枠番号が決める
    DroneFormationType ignored;
    return TryGetDroneFormation(id, ignored);
}

// 当たり判定を持つ陣形（技）か。表の Role 列を引くので、隊形を足してもここは触らない
inline bool IsDroneAttackFormation(ActionID id)
{
    // データの技は当たり判定を持つ攻撃技として作る前提（攻撃しない隊形は C++ 側にある）。
    if (id == A_DroneFormation) return true;

    switch (id)
    {
#define CCL_BT_DRONE_ROLE(Name, Id, Formation, Role, Display) \
    case Id:                                                  \
        return DroneActionRole::Role == DroneActionRole::Attack;
        BT_DRONE_ACTION_LIST(CCL_BT_DRONE_ROLE)
#undef CCL_BT_DRONE_ROLE
    default:
        return false;
    }
}

inline bool IsWaitAction(ActionID id) { return id == A_Wait; }

// BossCommandComponent::currentActionId を毎フレーム書く行動か（Parallel の従に置いてはいけない行動）
inline bool IsCommandWritingAction(ActionID id)
{
    const ActionEntry *e = FindActionEntry(id);
    return e && e->kind == BTActionKind::Driving;
}

// Running を返しうる行動か（即 Success の行動だけの枝を読み込み時に見つけるのに使う）
inline bool IsBlockingAction(ActionID id)
{
    const ActionEntry *e = FindActionEntry(id);
    return e && e->kind != BTActionKind::Instant;
}

// この行動が使う param の意味。使わないなら nullptr（エディタの入力欄の有無）
inline const char *GetActionParamLabel(ActionID id)
{
    const ActionEntry *e = FindActionEntry(id);
    return (e && e->paramLabel && e->paramLabel[0]) ? e->paramLabel : nullptr;
}

// この条件が使う param の意味。使わないなら nullptr
inline const char *GetConditionParamLabel(ActionID id)
{
    const ActionEntry *e = FindConditionEntry(id);
    return (e && e->paramLabel && e->paramLabel[0]) ? e->paramLabel : nullptr;
}
