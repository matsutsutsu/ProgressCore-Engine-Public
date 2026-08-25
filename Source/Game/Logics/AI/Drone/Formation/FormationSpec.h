/// @file  FormationSpec.h
/// @brief 陣形1つぶんの「データ」。C++ で書いていた陣形クラスをパラメータへ開いたもの。
///
/// なぜ必要か:
///   既存の攻撃陣形を分解すると、どれも
///     ①localIndex を席へ割り付け ②席から座標を作り ③形全体を1つ動かす
///   の3段で、違うのは数値と軸だけだった。C++ に書かれているとその数値を1つ変える
///   たびにリビルドが要る。ここをデータにすると実行中に変えられる。
///
/// カバー範囲（意図的な線引き）:
///   「形が attackProgress の関数で決まる」技だけを対象にする。
///   KamikazeSwarm（各機が飛距離で自分から切り上げる）と SquadAssault（1つ前の機体に
///   追従する）は個体がフィールドへ状態をラッチするので、この形式では書けない。
///   OrbitCircle 系は進行度ではなく経過時間で回るのでこれも対象外。
///   ＝ ChargeLifecycle::Broadcast の陣形だけがデータになる、と同じ線引きになる。
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

#include <json.hpp>

#include "Engine/Core/Math/CurveData.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

namespace CCL::AI::Formation
{

/// @brief 陣形の基底3軸。Forward は「ボス→プレイヤーの水平方向」で毎フレーム作り直す。
enum class SpecAxis : uint8_t
{
    Right,
    Up,
    Forward,
};

/// @brief 席の並べ方。
enum class SpecShape : uint8_t
{
    Grid, ///< 直交格子。列は columnAxis 方向、行は上方向（刃・壁）
    Arm,  ///< 放射状。列は「腕の向き」への距離、行は上方向（腕・薙ぎ払い）
};

/// @brief 形全体にかける動き。既存の攻撃陣形はこの3つしか使っていない。
enum class SpecTransform : uint8_t
{
    None,
    Hinge,     ///< 下端を軸に up→forward へ倒す（縦割り・波状縦割り）
    YawSweep,  ///< 腕の向きを Y 軸まわりに振る（挟み込み・薙ぎ払い）
    Translate, ///< 形ごと平行移動する（2段挟み込みが左右から寄る）
};

/// @brief localIndex をグループへ配る順序。
enum class SpecSplit : uint8_t
{
    Interleaved, ///< 1機ずつ交互に配る（i % groupCount）
    Block,       ///< 連続したかたまりで配る（i / perGroup）
};

/// @brief 時間差を「何の単位で」つけるか。
enum class SpecPhaseSource : uint8_t
{
    None,   ///< 全機同時に動く
    Column, ///< 列ごとにずらす（波状縦割り＝波が横へ走る）
    Tier,   ///< 段ごとにずらす（2段挟み込み＝上下の先攻後攻）
};

/// @brief 機首の向け方。
///
/// Auto で足りるのは、向きが形と変換からほぼ一意に決まるため。
///   ヒンジ → 倒れていく接線 / 腕 → 腕の伸びる向き / 平行移動 → 動く向きの逆（内向き）
/// 例外は挟み込みで、腕自体は横を向いたままプレイヤーへ機首を向ける。
enum class SpecLook : uint8_t
{
    Auto,
    ToPlayer, ///< プレイヤーへ水平に（挟み込み）
};

// ============================================================================
// 陣形データ本体
// ============================================================================

/// @brief 陣形1つぶんのパラメータ。1ファイル = 1陣形（Assets/Formations/*.json）。
struct FormationSpec
{
    std::string name = "Formation";

    // ------------------------------------------------------------------
    // ① 割付：localIndex → 席（グループ・列・行）
    // ------------------------------------------------------------------
    struct Assign
    {
        /// 符号で分かれるグループ数。左右の腕なら 2。変換の向きが反転する。
        int sideGroups = 1;

        /// 時間差で動くグループ数。上下2段なら 2。phaseSource=Tier のときの単位になる。
        int tierGroups = 1;

        SpecSplit split = SpecSplit::Block;

        /// 格子の行数。0 なら「正方に近い自動格子（列が先に進む）」。
        ///
        /// 0 と 1以上で列・行の進み方が逆になる。既存陣形がそう書かれていたため:
        ///   縦割り  … cols=√総数 を出して col=i%cols（列が先）＝刃を横へ敷き詰める
        ///   それ以外… rows は固定値で row=i%rows（行が先）＝1列を縦に埋めてから隣の列へ
        int rows = 0;
    } assign;

    // ------------------------------------------------------------------
    // ② 形：席 → 座標
    // ------------------------------------------------------------------
    struct Shape
    {
        SpecShape type = SpecShape::Grid;

        /// Grid のとき列を並べる軸。縦割りは Right（横へ敷く）、2段挟み込みは Forward（奥行き）。
        SpecAxis columnAxis = SpecAxis::Right;

        float spacingA = 2.0f; ///< 列方向の間隔 [m]（Arm では半径方向の間隔）
        float spacingB = 2.0f; ///< 行方向（上方向）の間隔 [m]

        /// 列方向を中央揃えにするか。
        ///   true  … 形の中心がボス正面に来る（刃・壁）
        ///   false … baseA から片側へ伸びる（腕）
        bool  centerColumns = true;
        float baseA         = 0.0f; ///< centerColumns=false のときの列方向の起点（Arm の最内半径）

        float baseB     = 0.0f; ///< 行方向の起点＝形の下端 [m]
        float tierStepB = 0.0f; ///< 段が1つ上がるごとに足す高さ [m]（上下2段の段差）

        /// ボスからの原点オフセット。
        float originForward = 0.0f;
        float originUp      = 0.0f;
        float originRight   = 0.0f;
    } shape;

    // ------------------------------------------------------------------
    // ③ 変換：形全体をどう動かすか（1つだけ）
    //
    // 複数を重ねられるようにしない理由:
    //   回転→平行移動と平行移動→回転は別物なので、順序という概念が増える。
    //   既存の攻撃陣形はすべて変換1つで足りているので、必要になるまで持たない。
    // ------------------------------------------------------------------
    struct Transform
    {
        SpecTransform type = SpecTransform::None;

        /// Translate のときの移動軸。Hinge は「right を軸に up→forward」で固定、
        /// YawSweep は「Y 軸まわり」で固定なので参照しない。
        SpecAxis axis = SpecAxis::Right;

        float from = 0.0f; ///< 進行度0での値。Hinge/YawSweep は角度[度]、Translate は距離[m]
        float to   = 0.0f; ///< 進行度1での値

        /// side グループごとに符号を反転するか（左右の腕が対称に動く）。
        bool mirrorBySide = false;
        /// 反転の向きを入れ替える。side=0 を負側にしたいときに立てる。
        bool mirrorFlip = false;
    } transform;

    // ------------------------------------------------------------------
    // ④ 時間：進行度をどう変換量へ写すか
    // ------------------------------------------------------------------
    struct Timing
    {
        float speed = 2.5f; ///< attackProgress の進む速さ [1/秒]。1/speed 秒で振り切る

        /// 進行度(0→1) → 変換量(0→1)。キーが無ければ p*p（既存陣形と同じ easeIn）。
        ///
        /// 既存陣形は eased = p*p で固定されており、「タメてから一気に」
        /// 「途中で止めて見せる」が原理的に作れなかった。ここをカーブにすると、
        /// その表現が初めてデータ側で作れるようになる。
        CurveData curve;

        SpecPhaseSource phaseSource  = SpecPhaseSource::None;
        float           phaseOverlap = 1.0f;  ///< 1単位が動き切るのに使う進行度の幅（0.35＝波が細い）
        bool            phaseReverse = false; ///< 順序を反転（波が左→右／右→左）
    } timing;

    // ------------------------------------------------------------------
    // ⑤ 予備動作の見せ（Tell）：どれが先に来るかをプレイヤーへ読ませる震え
    // ------------------------------------------------------------------
    struct Tell
    {
        bool     enabled   = false;
        SpecAxis axis      = SpecAxis::Right;
        float    amplitude = 0.0f;  ///< 震え幅 [m]
        float    frequency = 10.0f; ///< 震えの速さ [rad/秒]

        /// 「先に動く側」と見なす phaseT の上限。これ以下の席だけが震える。
        float leadThreshold = 0.18f;
    } tell;

    // ------------------------------------------------------------------
    // ⑥ 出力
    // ------------------------------------------------------------------
    SpecLook   look       = SpecLook::Auto;
    ChargeMode chargeMode = ChargeMode::RigidFollow;

    bool  clampToGround   = false; ///< 水平を越えて倒れる形が地面へ潜るのを止める
    float groundClearance = 0.5f;  ///< ボス足元からの最低高さ [m]
};

// ============================================================================
// JSON（CurveData と同じ作法。エディタの保存形式とランタイムの読み込みを一致させる）
// ============================================================================

inline void to_json(nlohmann::json &j, const FormationSpec &s)
{
    j = nlohmann::json{
        {"name", s.name},
        {"assign",
            {
                {"sideGroups", s.assign.sideGroups},
                {"tierGroups", s.assign.tierGroups},
                {"split", (int)s.assign.split},
                {"rows", s.assign.rows},
            }},
        {"shape",
            {
                {"type", (int)s.shape.type},
                {"columnAxis", (int)s.shape.columnAxis},
                {"spacingA", s.shape.spacingA},
                {"spacingB", s.shape.spacingB},
                {"centerColumns", s.shape.centerColumns},
                {"baseA", s.shape.baseA},
                {"baseB", s.shape.baseB},
                {"tierStepB", s.shape.tierStepB},
                {"originForward", s.shape.originForward},
                {"originUp", s.shape.originUp},
                {"originRight", s.shape.originRight},
            }},
        {"transform",
            {
                {"type", (int)s.transform.type},
                {"axis", (int)s.transform.axis},
                {"from", s.transform.from},
                {"to", s.transform.to},
                {"mirrorBySide", s.transform.mirrorBySide},
                {"mirrorFlip", s.transform.mirrorFlip},
            }},
        {"timing",
            {
                {"speed", s.timing.speed},
                {"curve", s.timing.curve},
                {"phaseSource", (int)s.timing.phaseSource},
                {"phaseOverlap", s.timing.phaseOverlap},
                {"phaseReverse", s.timing.phaseReverse},
            }},
        {"tell",
            {
                {"enabled", s.tell.enabled},
                {"axis", (int)s.tell.axis},
                {"amplitude", s.tell.amplitude},
                {"frequency", s.tell.frequency},
                {"leadThreshold", s.tell.leadThreshold},
            }},
        {"look", (int)s.look},
        {"chargeMode", (int)s.chargeMode},
        {"clampToGround", s.clampToGround},
        {"groundClearance", s.groundClearance},
    };
}

inline void from_json(const nlohmann::json &j, FormationSpec &s)
{
    s.name = j.value("name", std::string("Formation"));

    if (j.contains("assign"))
    {
        const auto &a       = j["assign"];
        s.assign.sideGroups = a.value("sideGroups", 1);
        s.assign.tierGroups = a.value("tierGroups", 1);
        s.assign.split      = (SpecSplit)a.value("split", (int)SpecSplit::Block);
        s.assign.rows       = a.value("rows", 0);
    }
    if (j.contains("shape"))
    {
        const auto &h         = j["shape"];
        s.shape.type          = (SpecShape)h.value("type", (int)SpecShape::Grid);
        s.shape.columnAxis    = (SpecAxis)h.value("columnAxis", (int)SpecAxis::Right);
        s.shape.spacingA      = h.value("spacingA", 2.0f);
        s.shape.spacingB      = h.value("spacingB", 2.0f);
        s.shape.centerColumns = h.value("centerColumns", true);
        s.shape.baseA         = h.value("baseA", 0.0f);
        s.shape.baseB         = h.value("baseB", 0.0f);
        s.shape.tierStepB     = h.value("tierStepB", 0.0f);
        s.shape.originForward = h.value("originForward", 0.0f);
        s.shape.originUp      = h.value("originUp", 0.0f);
        s.shape.originRight   = h.value("originRight", 0.0f);
    }
    if (j.contains("transform"))
    {
        const auto &t            = j["transform"];
        s.transform.type         = (SpecTransform)t.value("type", (int)SpecTransform::None);
        s.transform.axis         = (SpecAxis)t.value("axis", (int)SpecAxis::Right);
        s.transform.from         = t.value("from", 0.0f);
        s.transform.to           = t.value("to", 0.0f);
        s.transform.mirrorBySide = t.value("mirrorBySide", false);
        s.transform.mirrorFlip   = t.value("mirrorFlip", false);
    }
    if (j.contains("timing"))
    {
        const auto &t         = j["timing"];
        s.timing.speed        = t.value("speed", 2.5f);
        s.timing.phaseSource  = (SpecPhaseSource)t.value("phaseSource", (int)SpecPhaseSource::None);
        s.timing.phaseOverlap = t.value("phaseOverlap", 1.0f);
        s.timing.phaseReverse = t.value("phaseReverse", false);
        if (t.contains("curve")) s.timing.curve = t["curve"].get<CurveData>();
    }
    if (j.contains("tell"))
    {
        const auto &t        = j["tell"];
        s.tell.enabled       = t.value("enabled", false);
        s.tell.axis          = (SpecAxis)t.value("axis", (int)SpecAxis::Right);
        s.tell.amplitude     = t.value("amplitude", 0.0f);
        s.tell.frequency     = t.value("frequency", 10.0f);
        s.tell.leadThreshold = t.value("leadThreshold", 0.18f);
    }

    s.look            = (SpecLook)j.value("look", (int)SpecLook::Auto);
    s.chargeMode      = (ChargeMode)j.value("chargeMode", (int)ChargeMode::RigidFollow);
    s.clampToGround   = j.value("clampToGround", false);
    s.groundClearance = j.value("groundClearance", 0.5f);
}

} // namespace CCL::AI::Formation
