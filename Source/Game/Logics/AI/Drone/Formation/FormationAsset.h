// 陣形1つぶんのデータ（Assets/Formations/*.json）。
//
// 責務はデータの形を決めることだけ。
//   各項目が何を意味するか（画面に出る文言）は FormationAssetMeta.h の記述子が持つ。
//   保存と検証は FormationAssetIO、座標の計算は Formations_DataDriven。
//   同じ説明を2箇所に書くと必ず片方が腐るので、ここには「なぜこの形か」だけ書く。
//
// なぜデータにするか:
//   既存の攻撃陣形（Formations_Attack.h・8クラス492行）を分解すると、例外なく
//     (1) 割付 通し番号 → 席(group, col, row)
//     (2) 形   席 → ローカル座標
//     (3) 動き ローカル座標を順に加工する
//     (4) 配置 ボスの位置と向きへ乗せる
//   の4段で、違うのは数値と(3)の中身だけだった。4段を C++ に1回だけ書き、
//   残りをデータにすれば、技を1つ増やすのにリビルドが要らなくなる。
//
// layers を配列にした理由（2026-08 の実装が詰まった点）:
//   前回の FormationSpec は変換を1つしか持てず、「既存陣形はすべて変換1つで
//   足りている」と書かれていた。実際には ClapSlam が「腕を回す」と「横へずらす」を
//   同時にかけている。腕は放射状なので、角度を残したまま閉じると先端ほど隙間が開く。
//   角度を 0 まで戻し切ったうえで横へ逃がす、という2段構えが技の成立条件そのもので、
//   変換1つでは原理的に書けなかった。よって配列にし、順序に意味を持たせる
//   （回転→移動と移動→回転は着地点が違う）。
//
// 画面での見せ方をデータ側に寄せてある:
//   「速さ 2.5 /秒」ではなく「振り切るまで 0.40 秒」を持つ、のように、UI で変換や
//   合成をしなくて済む形で持つ。UI 側で翻訳すると、その翻訳コードを画面と JSON の
//   両方に書くことになり、フィールドを1つ足すたびに3箇所を触る羽目になる。
//
// カバー範囲（意図的な線引き）:
//   「形が attackProgress の関数で決まる」技だけ。KamikazeSwarm（各機が飛距離で
//   切り上げる）と SquadAssault（1つ前の機体の実位置に依存）は個体がフィールドへ
//   状態をラッチするので書けない。＝ ChargeLifecycle::Broadcast の陣形だけがデータになる。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Engine/Core/Math/CurveData.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

namespace CCL::AI::Formation
{

    // 陣形ローカル空間の3軸。Forward は「ボス→プレイヤーの水平方向」で毎フレーム作り直す
    // ので、陣形はワールド座標を知らずに「前・横・上」だけで書ける。
    enum class AssetAxis : uint8_t
    {
        Right,
        Up,
        Forward,
    };

    // 通し番号をグループへ配る順序。
    enum class AssetSplit : uint8_t
    {
        Block,       // 0 0 0 0 1 1 1 1
        Interleaved, // 0 1 0 1 0 1 0 1
    };

    // 格子の埋め方。前回はこれを rows の 0/非0 で兼用していたため、「行数」と「埋め順」を
    // 独立に指定できなかった（縦割りだけが列先だったのでそう書くしかなかった）。
    enum class AssetFillOrder : uint8_t
    {
        ColumnFirst,
        RowFirst,
    };

    // 形にかける動き1枚の種類。既存陣形を分解するとこの3つで足りた。
    //   前回の Hinge（倒す）と YawSweep（振る）は「ローカル原点まわりの回転」で、
    //   違いは軸だけなので Rotate 1種に統合してある。軸に Forward を選べば
    //   既存に無い「横へ倒れる刃」も出せる。
    enum class LayerType : uint8_t
    {
        Rotate,
        Translate,
        Shiver,
    };

    // 動き出す時刻を「何の単位で」ずらすか。
    //   波状縦割りの「波」と挟み込み2段の「先攻後攻」は、コード上まったく同じ式だった
    //   （start = 席の位置 × (窓幅 - duration)）。ずらす単位が違うだけなのでここで吸収する。
    enum class PhaseSource : uint8_t
    {
        None,
        Column,
        Row,
        Group,
    };

    // 機首の向け方。Auto で足りるのは、向きが形と動きからほぼ一意に決まるため。
    enum class LookMode : uint8_t
    {
        Auto,
        ToPlayer,
        Inward,
    };

    // (1) 割付：通し番号 → 席
    struct FormationAssign
    {
        int            sideGroups = 1;
        int            tierGroups = 1;
        AssetSplit     split      = AssetSplit::Block;
        int            rows       = 0; // 0 なら機数から正方に近い格子を組む
        AssetFillOrder fillOrder  = AssetFillOrder::RowFirst;
    };

    // (2) 形：席 → ローカル座標（動かす前の待機の形）
    //   列方向 = columnAxis、行方向は常に Up。前回あった Arm（放射状）は
    //   「columnAxis=Forward の格子」を Rotate レイヤーが振り回したものと同じ式だったので廃した。
    struct FormationLayout
    {
        AssetAxis columnAxis    = AssetAxis::Right;
        bool      centerColumns = true;
        float     baseA         = 0.0f;
        float     spacingA      = 2.5f;
        float     baseB         = 0.0f;
        float     spacingB      = 2.5f;
        float     tierStepB     = 0.0f;
    };

    // (3) 動き：形を加工する1枚
    //   種類ごとに型を分けず1構造体にしてあるのは、分けると JSON もエディタも
    //   分岐だらけになり、並べ替えの操作が書きにくくなるため。
    //   使わないフィールドが出るのは承知のうえで、配列として扱いやすい形を採っている。
    struct FormationLayer
    {
        LayerType type = LayerType::Rotate;
        AssetAxis axis = AssetAxis::Right;

        float from = 0.0f; // Rotate は角度[度]、Translate は距離[m]、Shiver は未使用
        float to   = 0.0f; // Shiver はここを振れ幅[m]として読む

        float windowBegin = 0.0f;
        float windowEnd   = 1.0f;

        /// 1席（1列・1段）が動き切るのに使う量。
        ///   start = windowBegin + 席の位置 × ((windowEnd - windowBegin) - duration)
        /// 最後の席がちょうど windowEnd で動き終わるように開始を配る、という式。
        float duration = 1.0f;

        PhaseSource phaseSource  = PhaseSource::None;
        bool        phaseReverse = false;

        /// 進行度 → 変換量。キーが無ければ p*p（easeIn）。既存陣形は全技がこれで固定
        /// されていて、「溜めてから一気に」が原理的に作れなかった。
        CurveData curve;

        bool mirror = false;

        // Shiver 専用
        float frequency     = 10.0f;
        float leadThreshold = 0.18f;
    };

    // (4) 配置：ボスからのオフセット
    //   レイヤーの回転はローカル原点まわりなので、ここが「回転軸の位置」になる。
    //   回す量(layer)と軸の位置(origin)が別々なので、片方だけ調整できる。
    struct FormationOrigin
    {
        float forward = 0.0f;
        float up      = 0.0f;
        float right   = 0.0f;
    };

    // 当たり方と見せ方（形そのものではない）
    struct FormationOutput
    {
        LookMode   look       = LookMode::Auto;
        ChargeMode chargeMode = ChargeMode::RigidFollow;

        bool clampToGround      = false;
        bool useShiverTelegraph = true;

        FormationCombat combat;
    };

    // 陣形1つぶん
    struct FormationAsset
    {
        std::string name = "Formation";

        /// 名乗る DroneFormationType の枠（kDataFormationSlotFirst 〜 Last）。
        /// BT もプレハブもこの数値で技を指すので、一度公開したら変えない
        /// （変えると保存済みの木が別の技を指す。BTAsset の assetId と同じ扱い）。
        uint8_t slot = 0;

        /// 読み込み元のファイル。エディタの保存先になる。JSON には書かない
        /// （ファイル自身に自分のパスを持たせると、コピーしたとき互いを上書きし合う）。
        std::string sourcePath;

        FormationAssign             assign;
        FormationLayout             layout;
        std::vector<FormationLayer> layers;
        FormationOrigin             origin;

        /// 撃つ合図から形が動き切るまで [秒]。
        /// 「1秒あたりいくつ進むか」ではなく秒で持つのは、画面でも JSON でも
        /// そのまま読める値にするため（UI 側での逆数変換が要らなくなる）。
        float attackSeconds = 0.4f;

        FormationOutput output;
    };

} // namespace CCL::AI::Formation
