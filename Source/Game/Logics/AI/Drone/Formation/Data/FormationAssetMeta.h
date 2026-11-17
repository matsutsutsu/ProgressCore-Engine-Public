#pragma once
// ===========================================================================
// 概要: FormationAsset の各項目を、どう保存してどう画面に出すかを書いた表
//
// 備考: JSON の読み書きとインスペクタの両方がこの表を読むので、項目を増やすときはここへ1行足す
//       記述子は offsetof で位置を持つので、入れ子の構造体は別の表にする
//       CFIELD を使わず FD_* を直に並べるのは、clang-format が2つめ以降を階段状にずらすため
//       表に載らないもの: FormationLayer::curve（カーブの編集で扱う）、FormationAsset::sourcePath
// ===========================================================================
#include <iterator>
#include <vector>

#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Game/Logics/AI/Drone/Formation/Data/FormationAsset.h"

namespace CCL::AI::Formation::Meta
{

    // enum の値の名前。並びは enum の宣言順（添字がそのまま値）。JSON にもこの綴りで書くので、一度公開したら変えない
    inline constexpr const char *kAxisNames[]       = {"Right", "Up", "Forward"};
    inline constexpr const char *kShapeNames[]      = {"Grid", "Model"};
    inline constexpr const char *kSplitNames[]      = {"Block", "Interleaved"};
    inline constexpr const char *kFillOrderNames[]  = {"ColumnFirst", "RowFirst"};
    inline constexpr const char *kLayerTypeNames[]  = {"Rotate", "Translate", "Shiver"};
    inline constexpr const char *kPhaseNames[]      = {"None", "Column", "Row", "Group"};
    inline constexpr const char *kLookNames[]       = {"Auto", "ToPlayer", "Inward"};
    inline constexpr const char *kChargeModeNames[] = {"Straight", "RigidFollow", "SoftFollow"};

    // 名前テーブルの要素数を FD_EnumU8 が取る形(int)で返す
    template <size_t N>
    constexpr int NameCount(const char *const (&)[N])
    {
        return static_cast<int>(N);
    }

    // (1) 割付: 通し番号 → 席
    inline const std::vector<FieldDescriptor> &AssignFields()
    {
        using T                                          = FormationAssign;
        static const std::vector<FieldDescriptor> fields = {
            FD_Int("左右のグループ数", "sideGroups", offsetof(T, sideGroups), 1, 8, nullptr, true, "2 にすると左右から挟み込む形になる"),
            FD_Int("上下のグループ数", "tierGroups", offsetof(T, tierGroups), 1, 8, nullptr, true, "2 にすると上段・下段に分かれ、先攻後攻を付けられる"),
            FD_EnumU8("グループへの配り方", "split", offsetof(T, split), kSplitNames, NameCount(kSplitNames), nullptr, true, "Block=前半と後半で分ける / Interleaved=1機ずつ交互に分ける"),
            FD_Int("行数", "rows", offsetof(T, rows), 0, 32, nullptr, true, "0 なら機数から正方に近い格子を自動で組む"),
            FD_EnumU8("格子の埋め順", "fillOrder", offsetof(T, fillOrder), kFillOrderNames, NameCount(kFillOrderNames), nullptr, true, "ColumnFirst=横へ1行を埋めてから上の行へ / RowFirst=縦へ1列を埋めてから隣の列へ"),
        };
        return fields;
    }

    // (2) 形: 席 → ローカル座標（動かす前の待機の形）
    inline const std::vector<FieldDescriptor> &LayoutFields()
    {
        using T                                          = FormationLayout;
        static const std::vector<FieldDescriptor> fields = {
            FD_EnumU8("形の作り方", "shape", offsetof(T, shape), kShapeNames, NameCount(kShapeNames), nullptr, true, "Grid=格子に並べる / Model=モデルの表面に並べる（下の Model 用の項目を使う）"),
            FD_String("モデル [Model]", "modelPath", offsetof(T, modelPath), nullptr, true, "形の元にするモデル。陣形エディタの「参照...」で選ぶ。変えると次のフレームで点を取り直す"),
            FD_Float("モデルの倍率 [Model]", "modelScale", offsetof(T, modelScale), 0.1f, 0.01f, 100.0f, nullptr, true, "ドローンは見た目 約3m。剣なら 15〜20m になる倍率にしないと点が詰まって形が読めない"),
            FD_Float("ピッチ [度] [Model]", "modelPitch", offsetof(T, modelPitch), 1.0f, 0.0f, 0.0f, nullptr, true, "Right 軸まわりの回転。構えた形の向きを決める"),
            FD_Float("ヨー [度] [Model]", "modelYaw", offsetof(T, modelYaw), 1.0f, 0.0f, 0.0f, nullptr, true, "Up 軸まわりの回転"),
            FD_Float("ロール [度] [Model]", "modelRoll", offsetof(T, modelRoll), 1.0f, 0.0f, 0.0f, nullptr, true, "Forward 軸まわりの回転"),
            FD_EnumU8("列を並べる向き", "columnAxis", offsetof(T, columnAxis), kAxisNames, NameCount(kAxisNames), nullptr, true, "行方向は常に上"),
            FD_Bool("列を左右対称に置く", "centerColumns", offsetof(T, centerColumns), nullptr, true, "原点を挟んで並べる。切ると片側へ伸びる"),
            FD_Float("列方向の起点 [m]", "baseA", offsetof(T, baseA), 0.1f, 0.0f, 0.0f, nullptr, true, "原点から最初の列までの距離"),
            FD_Float("列の間隔 [m]", "spacingA", offsetof(T, spacingA), 0.1f, 0.0f, 0.0f, nullptr, true, "ドローンは見た目 約3m。3 未満だと画面上で1枚の塊に潰れる"),
            FD_Float("行方向の起点 [m]", "baseB", offsetof(T, baseB), 0.1f, 0.0f, 0.0f, nullptr, true, "原点から最初の行までの高さ"),
            FD_Float("行の間隔 [m]", "spacingB", offsetof(T, spacingB), 0.1f, 0.0f, 0.0f, nullptr, true, "ドローンは見た目 約3m"),
            FD_Float("段ごとのずらし量 [m]", "tierStepB", offsetof(T, tierStepB), 0.1f, 0.0f, 0.0f, nullptr, true, "上下グループ1つぶん、行方向へずらす量"),
        };
        return fields;
    }

    // (3) 動き: 形を加工する1枚
    inline const std::vector<FieldDescriptor> &LayerFields()
    {
        using T                                          = FormationLayer;
        static const std::vector<FieldDescriptor> fields = {
            FD_EnumU8("動きの種類", "type", offsetof(T, type), kLayerTypeNames, NameCount(kLayerTypeNames), nullptr, true, "Rotate=原点まわりに回す / Translate=軸方向へ平行移動 / Shiver=撃つ前の予告の震え（予告の区間に置く）"),
            FD_EnumU8("軸", "axis", offsetof(T, axis), kAxisNames, NameCount(kAxisNames), nullptr, true, "Rotate ならこの軸まわりに回る"),
            FD_Float("始まりの値", "from", offsetof(T, from), 0.5f, 0.0f, 0.0f, nullptr, true, "Rotate は角度[度] / Translate は距離[m] / Shiver は振れ幅[m]"),
            FD_Float("終わりの値", "to", offsetof(T, to), 0.5f, 0.0f, 0.0f, nullptr, true, "始まりの値からここまで、緩急をつけて変わる"),
            FD_Float("動く時間帯 開始", "windowBegin", offsetof(T, windowBegin), 0.01f, 0.0f, 1.0f, nullptr, true, "攻撃の長さを 0〜1 とした割合。Shiver だけは予告の長さに対して"),
            FD_Float("動く時間帯 終了", "windowEnd", offsetof(T, windowEnd), 0.01f, 0.0f, 1.0f, nullptr, true, "攻撃の長さを 0〜1 とした割合。Shiver だけは予告の長さに対して"),
            FD_Float("1席が動き切る量", "duration", offsetof(T, duration), 0.01f, 0.0f, 1.0f, nullptr, true, "時間帯より短くすると、その差が席ごとの開始のずれに配られる＝波になる"),
            FD_EnumU8("ずらす単位", "phaseSource", offsetof(T, phaseSource), kPhaseNames, NameCount(kPhaseNames), nullptr, true, "None=全機いっせい / Column=列ごと / Row=段ごと / Group=割付グループごと（先攻後攻）"),
            FD_Bool("ずらす向きを逆に", "phaseReverse", offsetof(T, phaseReverse), nullptr, true, "端から始めるか、反対の端から始めるか"),
            FD_Bool("左右で反転", "mirror", offsetof(T, mirror), nullptr, true, "左右グループごとに始まり／終わりの符号を入れ替える"),
            FD_Float("震えの速さ [rad/s]", "frequency", offsetof(T, frequency), 0.5f, 0.0f, 0.0f, nullptr, true, "Shiver 専用。6.28 で1秒に1往復（10 で約1.6往復）"),
            FD_Float("先に動く側の範囲", "leadThreshold", offsetof(T, leadThreshold), 0.01f, 0.0f, 1.0f, nullptr, true, "Shiver 専用。ずらし順がこれ以下の席だけ震える。1 なら全席、小さいほど先頭だけ"),
        };
        return fields;
    }

    // (4) 配置: ボスからのオフセット（レイヤーの回転軸の位置でもある）
    inline const std::vector<FieldDescriptor> &OriginFields()
    {
        using T                                          = FormationOrigin;
        static const std::vector<FieldDescriptor> fields = {
            FD_Float("前へ [m]", "forward", offsetof(T, forward), 0.1f),
            FD_Float("上へ [m]", "up", offsetof(T, up), 0.1f),
            FD_Float("右へ [m]", "right", offsetof(T, right), 0.1f),
        };
        return fields;
    }

    // 当たり方と痛さ
    inline const std::vector<FieldDescriptor> &CombatFields()
    {
        using T                                          = FormationCombat;
        static const std::vector<FieldDescriptor> fields = {
            FD_Float("ダメージ", "damage", offsetof(T, damage), 0.5f),
            FD_Float("ノックバック", "knockback", offsetof(T, knockback), 0.5f),
            FD_Float("ヒットストップ [秒]", "hitStopDuration", offsetof(T, hitStopDuration), 0.01f, 0.0f, 1.0f, nullptr, true, "0 で無効"),
            FD_Float("ヒットストップ中の時間倍率", "hitStopScale", offsetof(T, hitStopScale), 0.01f, 0.0f, 1.0f, nullptr, true, "0 で完全停止"),
            FD_Float("再ヒット間隔 [秒]", "rehitInterval", offsetof(T, rehitInterval), 0.05f, -1.0f, 10.0f, nullptr, true, "同じ機体が同じ相手にもう一度当たるまでの間隔。負なら突撃1回につき1ヒット"),
        };
        return fields;
    }

    // 見せ方
    inline const std::vector<FieldDescriptor> &OutputFields()
    {
        using T                                          = FormationOutput;
        static const std::vector<FieldDescriptor> fields = {
            FD_EnumU8("機首の向け方", "look", offsetof(T, look), kLookNames, NameCount(kLookNames), nullptr, true, "Auto=形と動きから決める / ToPlayer=プレイヤーを向く / Inward=陣形の中心を向く"),
            FD_EnumU8("突撃中の移動方式", "chargeMode", offsetof(T, chargeMode), kChargeModeNames, NameCount(kChargeModeNames), nullptr, true, "RigidFollow=形が崩れない / SoftFollow=バネで遅れて追う / Straight=直進"),
            FD_Bool("地面へ潜らせない", "clampToGround", offsetof(T, clampToGround), nullptr, true, "席の高さを持ち上げて地面より下へ行かせない"),
            FD_Bool("撃つ前に震わせる", "useShiverTelegraph", offsetof(T, useShiverTelegraph), nullptr, true, "全体のざわつき（移動側の共通の震え）。Shiver の帯とは別。切ると帯の震えだけになる"),
        };
        return fields;
    }

    // 陣形そのものの情報（入れ子でない部分）
    inline const std::vector<FieldDescriptor> &RootFields()
    {
        using T                                          = FormationAsset;
        static const std::vector<FieldDescriptor> fields = {
            FD_String("技の名前", "name", offsetof(T, name), nullptr, true, "一覧とインスペクタに出る表示名。自由に変えてよい"),
            FD_UInt8("枠番号", "slot", offsetof(T, slot), 1.0f, 0.0f, static_cast<float>(kDroneFormationSlotCount - 1), nullptr, true, "BT もプレハブもこの数値で技を指す。画面からは変えられない（登録先とずれるため）", true),
            FD_Float("攻撃の長さ [秒]", "attackSeconds", offsetof(T, attackSeconds), 0.01f, 0.01f, 10.0f, nullptr, true, "撃つ合図から形が動き切るまで"),
            FD_Float("予告の長さ [秒]", "telegraphSeconds", offsetof(T, telegraphSeconds), 0.01f, 0.05f, 10.0f, nullptr, true, "撃つ前に震える時間。BT の「ドローンの技」はこの時間が過ぎたら撃つ"),
            FD_Float("撃ってから戻すまで [秒]", "recoverySeconds", offsetof(T, recoverySeconds), 0.01f, 0.0f, 30.0f, nullptr, true, "攻撃と硬直の長さ。過ぎたらドローンを頭上円陣へ戻す"),
            FD_Float("戻してからの間 [秒]", "restSeconds", offsetof(T, restSeconds), 0.01f, 0.0f, 30.0f, nullptr, true, "ボスが次の技を選ぶまで待つ時間。0 ならすぐ選ぶ"),
        };
        return fields;
    }

}
