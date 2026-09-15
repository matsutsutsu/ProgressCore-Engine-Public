// 陣形1つぶんのデータ構造。C++ の陣形クラス（Formations_Attack.h）をパラメータへ開いたもの。
//
// 責務: 「どんな形が、どう動くか」を宣言するだけ。ここは保存形式を知らない
//       （JSON の読み書きと検証は FormationAssetIO）。計算も持たない（評価は
//       Formations_DataDriven）。3者が同じファイルに同居すると、数値を1つ足すたびに
//       エディタ・ランタイム・保存の3箇所を同時に触ることになる。
//
// なぜこの形か:
//   既存の攻撃陣形を全部分解すると、例外なく次の4段だった。
//     ① 割付 localIndex → 席(group, col, row)
//     ② 形   席 → ローカル座標
//     ③ 動き ローカル座標を順に加工する
//     ④ 配置 ボスの位置と向きへ乗せる
//   違うのは数値と③の中身だけなので、4段を C++ に1回だけ書き、残りをデータにする。
//
// ③を配列にした理由（2026-08 の実装が詰まった点）:
//   前回の FormationSpec は変換を1つしか持てず、「既存陣形はすべて変換1つで足りている」と
//   書かれていた。実際には ClapSlam が「腕を回す」と「横へずらす」の2つを同時にかけており、
//   データで再現できずプリセット選択に退行した。角度だけで閉じると腕が放射状なので先端ほど
//   隙間が開く＝角度を0に戻し切ったうえで横へ逃がす、という2段構えが技の成立条件そのもの。
//   よって layers は配列で、順序が意味を持つ（回転→移動と移動→回転は別の技になる）。
//
// カバー範囲（意図的な線引き）:
//   「形が attackProgress の関数で決まる」技だけ。KamikazeSwarm（各機が飛距離で切り上げる）と
//   SquadAssault（1つ前の機体の実位置に依存）は個体がフィールドへ状態をラッチするので書けない。
//   ＝ ChargeLifecycle::Broadcast の陣形だけがデータになる、と同じ線引き。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Engine/Core/Math/CurveData.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

namespace CCL::AI::Formation
{

// 陣形ローカル空間の3軸。Forward は「ボス→プレイヤーの水平方向」で毎フレーム作り直すため、
// 陣形はワールド座標を一切知らずに「前・横・上」だけで書ける。
enum class AssetAxis : uint8_t
{
    Right,
    Up,
    Forward,
};

// localIndex をグループへ配る順序。
enum class AssetSplit : uint8_t
{
    Block,       // 連続したかたまりで配る（0 0 0 0 1 1 1 1）。挟み込み2段
    Interleaved, // 1機ずつ交互に配る（0 1 0 1 0 1）。挟み込み1段
};

// 格子の埋め方。前回はこれを rows の 0/非0 で兼用していたため、「行数」と「埋め順」を
// 独立に指定できなかった（縦割りだけが列先だったのでそう書くしかなかった）。
enum class AssetFillOrder : uint8_t
{
    ColumnFirst, // 横に敷き詰めてから上へ。縦割り
    RowFirst,    // 1列を縦に埋めてから隣の列へ。それ以外
};

// 形にかける動き1枚の種類。既存陣形を分解するとこの3つで足りた。
//   Hinge（倒す）と YawSweep（振る）は「ローカル原点まわりの回転」で、違いは軸だけなので
//   Rotate 1種に統合してある。軸に Forward を選べば既存に無い「横へ倒れる刃」も出せる。
enum class LayerType : uint8_t
{
    Rotate,    // 軸まわりに回す。角度は度
    Translate, // 軸方向へ平行移動する。距離は m
    Shiver,    // 予備動作中だけ震える。「どちらが先に来るか」をプレイヤーへ読ませる
};

// 動き出すタイミングを「何の単位で」ずらすか。
//   波状縦割りの「波」と挟み込み2段の「先攻後攻」は、コード上まったく同じ式だった
//   （start = 席の位置 × (窓幅 - duration)）。ずらす単位が違うだけなのでここで吸収する。
enum class PhaseSource : uint8_t
{
    None,   // 全席同時
    Column, // 列ごと。波が横へ走る
    Row,    // 行ごと。波が上下へ走る
    Group,  // tier グループごと。上段/下段の先攻後攻
};

// 機首の向け方。Auto で足りるのは、向きが形と動きからほぼ一意に決まるため
// （回転なら接線、移動なら進む向き）。例外は挟み込みで、腕は横を向いたまま機首だけ内を向く。
enum class LookMode : uint8_t
{
    Auto,     // 回転レイヤーと同じだけ Forward を回す。倒れる刃は倒れる先を向く
    ToPlayer, // プレイヤーへ水平に（挟み込み1段）
    Inward,   // 左右対称の内側を向く。2本の壁が互いを向く（挟み込み2段）
};

// ---------------------------------------------------------------------------
// ① 割付：localIndex → 席
// ---------------------------------------------------------------------------
struct FormationAssign
{
    /// 左右対称に動かす単位。腕が2本なら 2。layer.mirror がこの番号から符号を取る。
    int sideGroups = 1;

    /// 先攻後攻をつける単位。上下2段なら 2。PhaseSource::Group のずらす単位になる。
    int tierGroups = 1;

    AssetSplit split = AssetSplit::Block;

    /// 格子の行数。0 なら総数から正方に近い格子を組む（縦割りが sqrt で出していた形）。
    int rows = 0;

    AssetFillOrder fillOrder = AssetFillOrder::RowFirst;
};

// ---------------------------------------------------------------------------
// ② 形：席 → ローカル座標（動かす前の「待機の形」）
// ---------------------------------------------------------------------------
//   列方向 = columnAxis、行方向は常に Up。前回あった Arm（放射状）は
//   「columnAxis=Forward の格子」を Rotate レイヤーが振り回したものと同じ式だったので廃した。
struct FormationLayout
{
    /// 列を並べる軸。刃・壁は Right（横に敷く）、腕は Forward（前へ伸ばす）。
    AssetAxis columnAxis = AssetAxis::Right;

    /// 列方向を中央揃えにするか。true＝形の中心がボス正面（刃）、false＝baseA から片側へ伸びる（腕）。
    bool centerColumns = true;

    float baseA    = 0.0f; // centerColumns=false のときの列方向の起点 [m]（腕の最内半径）
    float spacingA = 2.5f; // 列方向の機体間隔 [m]
    float baseB    = 0.0f; // 行方向の起点＝形の下端 [m]
    float spacingB = 2.5f; // 行方向（上）の機体間隔 [m]

    /// tier が1つ上がるごとに足す高さ [m]。挟み込み2段の上段と下段の段差。
    float tierStepB = 0.0f;
};

// ---------------------------------------------------------------------------
// ③ 動き：形を加工する1枚
// ---------------------------------------------------------------------------
//   使うフィールドは type で変わる。全部入りの1構造体にしてあるのは、種類ごとに型を分けると
//   JSON もエディタも分岐だらけになり、レイヤーを並べ替える操作が書きにくくなるため。
struct FormationLayer
{
    LayerType type = LayerType::Rotate;

    /// Rotate は回転軸、Translate/Shiver は動く向き。
    AssetAxis axis = AssetAxis::Right;

    float from = 0.0f; // 進行度0での値。Rotate は角度[度]、Translate は距離[m]
    float to   = 0.0f; // 進行度1での値。Shiver はここを振幅[m]として読む

    // ---- いつ動くか ----

    /// この層が使う全体進行度の区間。層ごとに分ければ「振りかぶってから叩きつける」が書ける。
    float windowBegin = 0.0f;
    float windowEnd   = 1.0f;

    /// 1席（1列・1段）が動き切るのに使う量。窓幅と同じなら全席同時、狭いほど波が細くなる。
    ///   start = windowBegin + 席の位置 × ((windowEnd - windowBegin) - duration)
    /// 最後の席がちょうど windowEnd で動き終わるように開始を配るのがこの式の意味。
    float duration = 1.0f;

    PhaseSource phaseSource  = PhaseSource::None;
    bool        phaseReverse = false; // 順序を反転。波の左右・段の先攻後攻はこの真偽差だけ

    /// 進行度 → 変換量。キーが無ければ従来どおり p*p（easeIn）。
    /// 既存陣形は全技がこの easeIn 固定で、「溜めてから一気に」が原理的に作れなかった。
    CurveData curve;

    /// side グループごとに符号を反転するか。左右2本の腕が対称に動く。
    bool mirror = false;

    // ---- Shiver 専用 ----

    float frequency = 10.0f; // 震えの速さ [rad/秒]。上げすぎるとノイズに見えて予告にならない

    /// 「先に動く側」とみなす席位置(0〜1)の上限。これ以下の席だけが震える。
    /// 席位置は phaseSource から取るので、波なら先頭の刃、2段なら先攻の段が震える。
    float leadThreshold = 0.18f;
};

// ---------------------------------------------------------------------------
// ④ 配置：ボスからのオフセット
// ---------------------------------------------------------------------------
//   レイヤーの回転はローカル原点まわりなので、「回す量」(layer) と「軸の位置」(origin) が
//   独立に触れる。縦割りの up=2.0 がそのまま蝶番の高さ。
struct FormationOrigin
{
    float forward = 0.0f;
    float up      = 0.0f;
    float right   = 0.0f;
};

// ---------------------------------------------------------------------------
// 出力（形そのものではなく、当たり方と見せ方）
// ---------------------------------------------------------------------------
struct FormationOutput
{
    LookMode   look       = LookMode::Auto;
    ChargeMode chargeMode = ChargeMode::RigidFollow;

    /// 水平を越えて倒れる形が地面へ潜るのを止める。
    bool clampToGround = false;

    /// 予告の震え（DroneShiverTuning による全機共通のもの）を出すか。
    /// 動きそのもので予告する技は切る＝予告が二重になってどちらを読むか濁るため。
    bool useShiverTelegraph = true;

    FormationCombat combat;
};

// ---------------------------------------------------------------------------
// 陣形1つぶん
// ---------------------------------------------------------------------------
struct FormationAsset
{
    std::string name = "Formation";

    /// 名乗る DroneFormationType の枠（kDataFormationSlotFirst 〜 Last）。
    /// BT もプレハブもこの数値で技を指すので、一度公開したら変えない
    /// （変えると保存済みの木が別の技を指す。BTAsset の assetId と同じ扱い）。
    uint8_t slot = 0;

    FormationAssign             assign;
    FormationLayout             layout;
    std::vector<FormationLayer> layers;
    FormationOrigin             origin;

    /// attackProgress の進む速さ [1/秒]。1/speed 秒で振り切る。
    float speed = 2.5f;

    FormationOutput output;
};

} // namespace CCL::AI::Formation
