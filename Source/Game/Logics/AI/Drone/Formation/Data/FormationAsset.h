#pragma once
// ===========================================================================
// 概要: データ陣形1つぶんの定義（FormationAsset）。Assets/Formations/*.json に保存する。
//
// 責務:
//   - (1)割付 (2)形 (3)動き (4)配置 の各段のパラメータを持つ
//   - 当たり方・機首の向け方・移動方式など、形以外の出力を持つ
//   - 名乗る枠番号・攻撃と予告の長さを持つ
//
// 備考: 解釈は DataDrivenFormation.h、読み書きは FormationAssetIO、項目の表は
//       FormationAssetMeta.h が担う。
// ===========================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "Engine/Core/Math/CurveData.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

namespace CCL::AI::Formation
{

    // 陣形ローカル空間の3軸。ボスから見た向きで、間隔も回転軸もこれで指す。
    enum class AssetAxis : uint8_t
    {
        Right,   // 横
        Up,      // 上
        Forward, // 前
    };

    // 通し番号をグループへ配る順序。
    enum class AssetSplit : uint8_t
    {
        Block,       // 0 0 0 0 1 1 1 1
        Interleaved, // 0 1 0 1 0 1 0 1
    };

    // 待機の形の作り方。
    enum class AssetShape : uint8_t
    {
        Grid,  // 格子に並べる（縦割り・挟み込みなど）
        Model, // モデルの表面に並べる（剣など）。点は ShapeSampler が取り出す
    };

    // 格子を埋めていく向き。名前は先に進む番号を指す（ColumnFirst＝列の番号が先に進む＝横へ並ぶ）
    enum class AssetFillOrder : uint8_t
    {
        ColumnFirst, // 横へ1行を埋めてから上の行へ
        RowFirst,    // 縦へ1列を埋めてから隣の列へ
    };

    // 形にかける動き1枚の種類
    enum class LayerType : uint8_t
    {
        Rotate,    // ローカル原点まわりに回す（軸を選べるので「倒す」も「振る」もこれで表す）
        Translate, // 軸方向へ平行移動する
        Shiver,    // 撃つ前の予告として小刻みに震わせる（時間帯は予告の中で数える）
    };

    // 動き出す時刻を「何の単位で」ずらすか。席ごとに開始をずらして波を作る。
    enum class PhaseSource : uint8_t
    {
        None,   // 全機いっせい
        Column, // 列ごとにずらす
        Row,    // 段ごとにずらす
        Group,  // 割付グループごとにずらす（先攻・後攻）
    };

    // 機首の向け方。多くの技は Auto で足りる
    enum class LookMode : uint8_t
    {
        Auto,     // 回転レイヤーが回した Forward を向く
        ToPlayer, // プレイヤーを向く（水平）
        Inward,   // 左右対称の内側を向く
    };

    // (1) 割付: 通し番号 → 席
    struct FormationAssign
    {
        int            sideGroups = 1;                 // 左右に分けるグループ数
        int            tierGroups = 1;                 // 段に分けるグループ数
        AssetSplit     split      = AssetSplit::Block; // 通し番号をグループへ配る順序
        int            rows       = 0; // 0 なら機数から正方に近い格子を組む
        AssetFillOrder fillOrder  = AssetFillOrder::RowFirst; // 格子を埋めていく向き
    };

    // (2) 形: 席 → ローカル座標（動かす前の待機の形）
    //   Grid: 列方向は columnAxis で選び、行方向は常に Up。
    //   Model: 通し番号の順にモデル表面の点へ並べる。格子の項目は使わない。
    struct FormationLayout
    {
        AssetShape shape = AssetShape::Grid; // 待機の形の作り方

        // --- Model 専用 ---

        std::string modelPath;          // 形の元にするモデル（描画と同じ .gltf）
        float       modelScale = 1.0f;  // モデルの大きさの倍率。実寸だと点が詰まって塊に見えるので拡大する

        // モデルの向き [度]。拡大の後、ロール→ピッチ→ヨーの順で回す（CreateFromYawPitchRoll と同じ順）
        float modelPitch = 0.0f;
        float modelYaw   = 0.0f;
        float modelRoll  = 0.0f;

        // --- Grid 専用 ---

        AssetAxis columnAxis    = AssetAxis::Right; // 列を並べる軸
        bool      centerColumns = true; // 列の並びを原点で挟んで左右対称にする
        float     baseA         = 0.0f; // 列方向の起点 [m]
        float     spacingA      = 2.5f; // 列と列の間隔 [m]
        float     baseB         = 0.0f; // 行方向の起点 [m]
        float     spacingB      = 2.5f; // 行と行の間隔 [m]
        float     tierStepB     = 0.0f; // 段グループ1つぶんの行方向オフセット [m]
    };

    // (3) 動き: 形を加工する1枚。
    // 種類ごとに型を分けず1構造体にして、JSON とエディタで配列として並べ替えやすくする（使わない項目は出る）
    struct FormationLayer
    {
        LayerType type = LayerType::Rotate; // 動きの種類
        AssetAxis axis = AssetAxis::Right;  // 回す・動かす・震わせる軸

        float from = 0.0f; // 始まりの量。Rotate は角度[度]、Translate は距離[m]、Shiver は振れ幅[m]
        float to   = 0.0f; // 終わりの量

        float windowBegin = 0.0f; // この層が動き始める時刻。攻撃の長さを 0〜1 とした割合（Shiver は予告の長さに対して）
        float windowEnd   = 1.0f; // この層が動き終わる時刻

        // 1席が動き切るのに使う量（窓に対する割合）。
        // start = windowBegin + 席の位置 × ((windowEnd - windowBegin) - duration) で、最後の席がちょうど windowEnd で終わる
        float duration = 1.0f;

        PhaseSource phaseSource  = PhaseSource::None; // 動き出しを何の単位でずらすか
        bool        phaseReverse = false;             // ずらす向きを逆にする（反対の端から始める）

        CurveData curve; // 進み → 変換量の緩急。キーが無ければ p*p（easeIn）

        bool mirror = false; // 側グループごとに from/to の符号を反転する

        // --- Shiver 専用 ---
        float frequency     = 10.0f; // 震えの速さ [rad/s]（sin の角速度。2π を掛けない）
        float leadThreshold = 0.18f; // ずらし順がこれ以下の席（先に動く側）だけを震わせる。1 なら全席
    };

    // (4) 配置: ボスからのオフセット [m]。動きの層の回転軸の位置になる
    struct FormationOrigin
    {
        float forward = 0.0f;
        float up      = 0.0f;
        float right   = 0.0f;
    };

    // 当たり方と見せ方（形そのものではない）
    struct FormationOutput
    {
        LookMode   look       = LookMode::Auto;          // 機首の向け方
        ChargeMode chargeMode = ChargeMode::RigidFollow; // 突撃中の移動方式

        bool clampToGround      = false; // 席が地面へ潜らないよう y を持ち上げる
        bool useShiverTelegraph = true;  // 撃つ前の震え（予告）を出すか

        FormationCombat combat; // 当たり方と痛さ
    };

    // 陣形1つぶん
    struct FormationAsset
    {
        std::string name = "Formation"; // 技の名前（エディタと BT に出る）

        // 名乗る DroneFormationType の枠。BT もプレハブもこの数値で技を指すので、一度公開したら変えない
        uint8_t slot = 0;

        // 読み込み元のファイル（エディタの保存先）。JSON には書かない（コピーしたとき互いを上書きし合うため）
        std::string sourcePath;

        FormationAssign             assign; // (1) 割付
        FormationLayout             layout; // (2) 形
        std::vector<FormationLayer> layers; // (3) 動き。並び順が適用順
        FormationOrigin             origin; // (4) 配置

        float attackSeconds = 0.4f; // 撃つ合図から形が動き切るまで [秒]

        // 撃つ前の予告の長さ [秒]。ボスBTの Wait（予告）と同じ値にする
        // （BT はこの値を読まない。ずれると、震えが途中で切れるか、震え終わっても撃たない）
        float telegraphSeconds = 1.0f;

        FormationOutput output; // 当たり方と見せ方
    };

} // namespace CCL::AI::Formation
