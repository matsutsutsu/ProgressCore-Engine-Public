#pragma once
// ドローンの陣形の種類と、その表示名テーブル
// DroneComponent.h から分けて、enum だけを使う側（BT のローダ・エディタ）の依存を軽くしている
#include <array>
#include <cstddef>
#include <cstdint>

// 陣形の一覧 (識別子, 保存値, 表示名)。enum も表示名もこの表から作る
//
// 保存値はプレハブとシーンに保存されるので手で書く。欠番は埋めず、増やすときは 49 以降
// 追加時は FormationRegistry への登録も要る（ないと指定しても静止するだけ）
//
// 33〜48 はデータ陣形（Assets/Formations/*.json）の予約枠。JSON の "slot" の番号へ起動時に入る
// 先に確保してあるので、エディタで技を作るたびにリビルドせずに済む
#define DRONE_FORMATION_LIST(X)                               \
    X(None, 0, "命令なし（現状維持）")                        \
    X(LowOrbit, 1, "低空円陣")                                \
    X(HighOrbit, 5, "頭上円陣")                               \
    X(Barrage, 17, "弾幕")                                    \
    X(SideSweep, 22, "横なぎ")                                \
    X(GuillotineSlam, 24, "縦割り")                           \
    X(ClapSlam, 25, "挟み込み1段")                            \
    X(DoubleClapSlam_UpperFirst, 27, "挟み込み2段・上段先攻") \
    X(DoubleClapSlam_LowerFirst, 28, "挟み込み2段・下段先攻") \
    X(SquadAssault, 32, "小隊突撃")                           \
    X(DataSlot00, 33, "[空き枠] 枠0")                         \
    X(DataSlot01, 34, "[空き枠] 枠1")                         \
    X(DataSlot02, 35, "[空き枠] 枠2")                         \
    X(DataSlot03, 36, "[空き枠] 枠3")                         \
    X(DataSlot04, 37, "[空き枠] 枠4")                         \
    X(DataSlot05, 38, "[空き枠] 枠5")                         \
    X(DataSlot06, 39, "[空き枠] 枠6")                         \
    X(DataSlot07, 40, "[空き枠] 枠7")                         \
    X(DataSlot08, 41, "[空き枠] 枠8")                         \
    X(DataSlot09, 42, "[空き枠] 枠9")                         \
    X(DataSlot10, 43, "[空き枠] 枠10")                        \
    X(DataSlot11, 44, "[空き枠] 枠11")                        \
    X(DataSlot12, 45, "[空き枠] 枠12")                        \
    X(DataSlot13, 46, "[空き枠] 枠13")                        \
    X(DataSlot14, 47, "[空き枠] 枠14")                        \
    X(DataSlot15, 48, "[空き枠] 枠15")

// ボスがドローン群へ出す「どの形を作れ」の指示。BossCommandComponent でも使う
enum class DroneFormationType : uint8_t
{
#define CCL_DRONE_FORMATION_ENUM(Name, Value, Display) Name = Value,
    DRONE_FORMATION_LIST(CCL_DRONE_FORMATION_ENUM)
#undef CCL_DRONE_FORMATION_ENUM
};

// 保存値の上限+1（欠番を含む）。インスペクタが値をそのまま添字にするので、名前テーブルはこの長さで作る
inline constexpr size_t kDroneFormationSlotCount = 49;

// 新しい技のための空き枠（両端を含む）。エディタの複製はここから空きを探す
inline constexpr uint8_t kDataFormationSlotFirst = 33;
inline constexpr uint8_t kDataFormationSlotLast  = 48;

// インスペクタ表示用の陣形名。欠番は "(欠番)" で埋める
static constexpr std::array<const char *, kDroneFormationSlotCount> MakeDroneFormationNames()
{
    std::array<const char *, kDroneFormationSlotCount> names{};
    for (auto &n : names)
        n = "(欠番)";
#define CCL_DRONE_FORMATION_NAME(Name, Value, Display) names[Value] = Display;
    DRONE_FORMATION_LIST(CCL_DRONE_FORMATION_NAME)
#undef CCL_DRONE_FORMATION_NAME
    return names;
}
inline constexpr auto kDroneFormationNames = MakeDroneFormationNames();
