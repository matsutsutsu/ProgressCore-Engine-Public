#pragma once
// ===========================================================================
// 概要: ドローンの陣形の種類（DroneFormationType）と、その表示名テーブル。
//
// 責務:
//   - 陣形の一覧（識別子・保存値・表示名）を1つの X マクロ表で持つ
//   - 表から enum と、インスペクタ用の名前テーブルを作る
//   - データ陣形の予約枠（33〜48）の範囲を定める
//
// 備考: DroneComponent.h から分けてあるのは、BT の技 ID から陣形を引くためにこの
//       enum を使う側（BTローダ・BTエディタ・ActionRegistry）が、ドローン1機ぶんの
//       状態とシリアライズ機構まで読み込まずに済むようにするため。
// ===========================================================================
#include <array>
#include <cstddef>
#include <cstdint>

// 陣形の一覧 (識別子, 保存値, 表示名)。enum も表示名もこの表から作る。
//   値を手で書くのは、この数値がプレハブとシーンに保存されているため。欠番は
//   削除した陣形の値なので埋めず、増やすときは 49 以降。追加時は FormationRegistry への
//   IFormation 登録も要る（無いと指定しても静止するだけ）。
//
// 33〜48 はデータ陣形（Assets/Formations/*.json）の予約枠。JSON 側が "slot" で
// 名乗った番号へ起動時に入る。枠を先に確保してあるのは、陣形を1つ作るたびに
// この表と ActionRegistry を編集してリビルドするのでは、エディタで組む意味が
// 半分消えるため。枠を使い切ったら 49 以降を足す。
#define DRONE_FORMATION_LIST(X)                             \
    X(Hidden, 0, "収納（命令なし）")                        \
    X(OrbitCircle, 1, "降下円陣")                           \
    X(HighOrbit, 5, "頭上円陣")                             \
    X(KamikazeSwarm, 17, "弾幕")                            \
    X(SideSlapWhip, 22, "横なぎ回転")                       \
    X(GuillotineSlam, 24, "縦割り")                         \
    X(ClapSlam, 25, "挟み込み1段")                          \
    X(TsunamiWave, 26, "波状縦割り・左→右")                 \
    X(MegaClapSlam_UpperFirst, 27, "挟み込み2段・上段先攻") \
    X(MegaClapSlam_LowerFirst, 28, "挟み込み2段・下段先攻") \
    X(TsunamiWave_RL, 31, "波状縦割り・右→左")              \
    X(SquadAssault, 32, "小隊突撃")                         \
    X(DataSlot00, 33, "[空き枠] 枠0")                       \
    X(DataSlot01, 34, "[空き枠] 枠1")                       \
    X(DataSlot02, 35, "[空き枠] 枠2")                       \
    X(DataSlot03, 36, "[空き枠] 枠3")                       \
    X(DataSlot04, 37, "[空き枠] 枠4")                       \
    X(DataSlot05, 38, "[空き枠] 枠5")                       \
    X(DataSlot06, 39, "[空き枠] 枠6")                       \
    X(DataSlot07, 40, "[空き枠] 枠7")                       \
    X(DataSlot08, 41, "[空き枠] 枠8")                       \
    X(DataSlot09, 42, "[空き枠] 枠9")                       \
    X(DataSlot10, 43, "[空き枠] 枠10")                      \
    X(DataSlot11, 44, "[空き枠] 枠11")                      \
    X(DataSlot12, 45, "[空き枠] 枠12")                      \
    X(DataSlot13, 46, "[空き枠] 枠13")                      \
    X(DataSlot14, 47, "[空き枠] 枠14")                      \
    X(DataSlot15, 48, "[空き枠] 枠15")

// ボスがドローン群へ出す「どの形を作れ」の指示。BossCommandComponent でも使う。
enum class DroneFormationType : uint8_t
{
#define CCL_DRONE_FORMATION_ENUM(Name, Value, Display) Name = Value,
    DRONE_FORMATION_LIST(CCL_DRONE_FORMATION_ENUM)
#undef CCL_DRONE_FORMATION_ENUM
};

// 保存値の上限+1（欠番を含む）。インスペクタが値をそのまま添字にするため、
// 名前テーブルは詰めた長さではなくこの長さで作る。
inline constexpr size_t kDroneFormationSlotCount = 49;

// 名前を持たない、新しい技のための空き枠（両端を含む）。エディタの複製はここから空きを探す。
// 名前のある枠（縦割り等）も JSON が名乗れる。C++ 実装のある枠だけは登録時に弾く。
inline constexpr uint8_t kDataFormationSlotFirst = 33;
inline constexpr uint8_t kDataFormationSlotLast  = 48;

// インスペクタ表示用の陣形名。欠番は "(欠番)" で埋める。
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
