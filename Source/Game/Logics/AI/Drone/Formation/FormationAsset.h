// 陣形1つぶんのデータ（Assets/Formations/*.json）。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Engine/Core/Math/CurveData.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

namespace CCL::AI::Formation
{

    /// 陣形ローカル空間の3軸。ボスから見た向きで、間隔も回転軸もこれで指す。
    enum class AssetAxis : uint8_t
    {
        Right,   // 横
        Up,      // 上
        Forward, // 前
    };

    /// 通し番号をグループへ配る順序。
    enum class AssetSplit : uint8_t
    {
        Block,       // 0 0 0 0 1 1 1 1
        Interleaved, // 0 1 0 1 0 1 0 1
    };

    /// 格子を埋めていく向き。行数(rows)とは独立に選ぶ。
    enum class AssetFillOrder : uint8_t
    {
        ColumnFirst, // 1列を縦に埋めてから次の列へ
        RowFirst,    // 1行を横に埋めてから次の行へ
    };

    /// 形にかける動き1枚の種類。
    ///   Rotate は軸を選べるので、「倒す」も「振る」もこれ1種で表せる。
    enum class LayerType : uint8_t
    {
        Rotate,    // ローカル原点まわりに回す
        Translate, // 軸方向へ平行移動する
        Shiver,    // 撃つ前の予告として小刻みに震わせる（時間帯は予告の中で数える）
    };

    /// 動き出す時刻を「何の単位で」ずらすか。席ごとに開始をずらして波を作る。
    enum class PhaseSource : uint8_t
    {
        None,   // 全機いっせい
        Column, // 列ごとにずらす
        Row,    // 段ごとにずらす
        Group,  // 割付グループごとにずらす（先攻・後攻）
    };

    /// 機首の向け方。Auto で足りるのは、向きが形と動きからほぼ一意に決まるため。
    enum class LookMode : uint8_t
    {
        Auto,
        ToPlayer,
        Inward,
    };

    /// (1) 割付：通し番号 → 席
    struct FormationAssign
    {
        int            sideGroups = 1;
        int            tierGroups = 1;
        AssetSplit     split      = AssetSplit::Block;
        int            rows       = 0; // 0 なら機数から正方に近い格子を組む
        AssetFillOrder fillOrder  = AssetFillOrder::RowFirst;
    };

    /// (2) 形：席 → ローカル座標（動かす前の待機の形）
    ///   列方向は columnAxis で選び、行方向は常に Up。
    struct FormationLayout
    {
        AssetAxis columnAxis    = AssetAxis::Right;
        bool      centerColumns = true; // 列の並びを原点で挟んで左右対称にする
        float     baseA         = 0.0f; // 列方向の起点 [m]
        float     spacingA      = 2.5f; // 列と列の間隔 [m]
        float     baseB         = 0.0f; // 行方向の起点 [m]
        float     spacingB      = 2.5f; // 行と行の間隔 [m]
        float     tierStepB     = 0.0f; // 段グループ1つぶんの行方向オフセット [m]
    };

    /// (3) 動き：形を加工する1枚
    ///   種類ごとに型を分けず1構造体にしてあるのは、分けると JSON もエディタも
    ///   分岐だらけになり、並べ替えの操作が書きにくくなるため。
    ///   使わないフィールドが出るのは承知のうえで、配列として扱いやすい形を採っている。
    struct FormationLayer
    {
        LayerType type = LayerType::Rotate;
        AssetAxis axis = AssetAxis::Right;

        float from = 0.0f; // Rotate は角度[度]、Translate は距離[m]、Shiver は振れ幅[m]
        float to   = 0.0f;

        float windowBegin = 0.0f; // この層が動く時間帯。攻撃の長さを 0〜1 とした割合（Shiver だけは予告の長さに対して）
        float windowEnd   = 1.0f;

        /// 1席（1列・1段）が動き切るのに使う量。
        ///   start = windowBegin + 席の位置 × ((windowEnd - windowBegin) - duration)
        /// 最後の席がちょうど windowEnd で動き終わるように開始を配る、という式。
        float duration = 1.0f;

        PhaseSource phaseSource  = PhaseSource::None;
        bool        phaseReverse = false; // ずらす向きを逆にする（端から／中央から）

        /// 進行度 → 変換量の効き方。キーが無ければ p*p（easeIn）。
        /// 「溜めてから一気に」のような緩急はここで作る。
        CurveData curve;

        bool mirror = false; // 側グループごとに from/to の符号を反転する

        // Shiver 専用
        float frequency     = 10.0f; // 震えの速さ [Hz]
        float leadThreshold = 0.18f; // ずらし順がこれ以下の席（先に動く側）だけを震わせる。1 なら全席
    };

    /// (4) 配置：ボスからのオフセット
    ///   レイヤーの回転はローカル原点まわりなので、ここが「回転軸の位置」になる。
    ///   回す量(layer)と軸の位置(origin)が別々なので、片方だけ調整できる。
    struct FormationOrigin
    {
        float forward = 0.0f;
        float up      = 0.0f;
        float right   = 0.0f;
    };

    /// 当たり方と見せ方（形そのものではない）
    struct FormationOutput
    {
        LookMode   look       = LookMode::Auto;
        ChargeMode chargeMode = ChargeMode::RigidFollow;

        bool clampToGround      = false; // 席が地面へ潜らないよう y を持ち上げる
        bool useShiverTelegraph = true;  // 撃つ前の震え（予告）を出すか

        FormationCombat combat;
    };

    /// 陣形1つぶん
    struct FormationAsset
    {
        std::string name = "Formation";

        /// 名乗る DroneFormationType の枠。名前のある枠（縦割り等）も空き枠も名乗れる。
        /// BT もプレハブもこの数値で技を指すので、一度公開したら変えない
        /// （変えると保存済みの木が別の技を指す。BTAsset の assetId と同じ扱い）。
        uint8_t slot = 0;

        /// 読み込み元のファイル。エディタの保存先になる。JSON には書かない
        /// （ファイル自身に自分のパスを持たせると、コピーしたとき互いを上書きし合う）。
        std::string sourcePath;

        FormationAssign             assign;
        FormationLayout             layout;
        std::vector<FormationLayer> layers; // 並び順が適用順
        FormationOrigin             origin;

        /// 撃つ合図から形が動き切るまで [秒]。
        /// 画面でも JSON でもそのまま読める単位にしてある。
        float attackSeconds = 0.4f;

        /// 撃つ前に震える予告の長さ [秒]。Shiver の時間帯はこの長さに対する割合で置く。
        /// ゲーム中に予告を何秒続けるかはボスBTの Wait が決めるので、同じ値にしておく
        /// （今は BT がこの値を読まない。ずれると、震えが途中で切れるか、震え終わっても撃たない）。
        float telegraphSeconds = 1.0f;

        FormationOutput output;
    };

} // namespace CCL::AI::Formation
