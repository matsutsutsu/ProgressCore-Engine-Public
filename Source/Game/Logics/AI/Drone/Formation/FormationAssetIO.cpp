// 陣形アセットの JSON 変換と検証の実装。
//
// enum を数値ではなく名前で保存する。数値だと "columnAxis": 2 と出て、意味を知るのに
// 毎回ヘッダを引くことになり、手で読み書きできるアセットという前提が崩れる。
//
// 読み込みは全フィールドがフォールバック付き。項目を後から足したときに既存の *.json が
// 読めなくなると、その時点で全アセットの手直しが要る。
#include "FormationAssetIO.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <json.hpp>

#include <algorithm>
#include <exception>
#include <vector>

// ChargeMode は DroneComponent.h がグローバル名前空間に置いている。nlohmann は型の属する
// 名前空間から変換関数を引く（ADL）ので、ここもグローバルに書かないと数値のまま保存される。
NLOHMANN_JSON_SERIALIZE_ENUM(ChargeMode, {
    {ChargeMode::Straight, "Straight"},
    {ChargeMode::RigidFollow, "RigidFollow"},
    {ChargeMode::SoftFollow, "SoftFollow"},
})

namespace CCL::AI::Formation
{

NLOHMANN_JSON_SERIALIZE_ENUM(AssetAxis, {
    {AssetAxis::Right, "Right"}, {AssetAxis::Up, "Up"}, {AssetAxis::Forward, "Forward"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(AssetSplit, {
    {AssetSplit::Block, "Block"}, {AssetSplit::Interleaved, "Interleaved"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(AssetFillOrder, {
    {AssetFillOrder::ColumnFirst, "ColumnFirst"}, {AssetFillOrder::RowFirst, "RowFirst"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(LayerType, {
    {LayerType::Rotate, "Rotate"}, {LayerType::Translate, "Translate"}, {LayerType::Shiver, "Shiver"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(PhaseSource, {
    {PhaseSource::None, "None"}, {PhaseSource::Column, "Column"},
    {PhaseSource::Row, "Row"},   {PhaseSource::Group, "Group"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(LookMode, {
    {LookMode::Auto, "Auto"}, {LookMode::ToPlayer, "ToPlayer"}, {LookMode::Inward, "Inward"},
})

// ---------------------------------------------------------------------------
// JSON 変換
// ---------------------------------------------------------------------------
// nlohmann が std::vector<FormationLayer> を自動で回せるよう、ADL で引ける名前で置く。

static void to_json(nlohmann::json &j, const FormationCombat &c)
{
    j = nlohmann::json{
        {"hitRadius", c.hitRadius},
        {"damage", c.damage},
        {"knockback", c.knockback},
        {"hitStopDuration", c.hitStopDuration},
        {"hitStopScale", c.hitStopScale},
    };
}

static void from_json(const nlohmann::json &j, FormationCombat &c)
{
    c.hitRadius       = j.value("hitRadius", 2.5f);
    c.damage          = j.value("damage", 8.0f);
    c.knockback       = j.value("knockback", 12.0f);
    c.hitStopDuration = j.value("hitStopDuration", 0.05f);
    c.hitStopScale    = j.value("hitStopScale", 0.0f);
}

static void to_json(nlohmann::json &j, const FormationLayer &l)
{
    j = nlohmann::json{
        {"type", l.type},
        {"axis", l.axis},
        {"from", l.from},
        {"to", l.to},
        {"window", {l.windowBegin, l.windowEnd}},
        {"duration", l.duration},
        {"phase", {{"source", l.phaseSource}, {"reverse", l.phaseReverse}}},
        {"mirror", l.mirror},
    };

    // キーが無いカーブ＝既定の easeIn。空の CurveData を毎層に書くと全レイヤーが
    // 10行ずつ膨らんで、手で読む前提の JSON が読めなくなる。
    if (!l.curve.empty()) j["curve"] = l.curve;

    // Shiver でしか意味を持たない値を常に書くと、他の層で「何の周波数だ」と読まれる。
    if (l.type == LayerType::Shiver)
    {
        j["frequency"]     = l.frequency;
        j["leadThreshold"] = l.leadThreshold;
    }
}

static void from_json(const nlohmann::json &j, FormationLayer &l)
{
    l.type = j.value("type", LayerType::Rotate);
    l.axis = j.value("axis", AssetAxis::Right);
    l.from = j.value("from", 0.0f);
    l.to   = j.value("to", 0.0f);

    if (j.contains("window") && j["window"].is_array() && j["window"].size() == 2)
    {
        l.windowBegin = j["window"][0].get<float>();
        l.windowEnd   = j["window"][1].get<float>();
    }
    l.duration = j.value("duration", 1.0f);

    if (j.contains("phase"))
    {
        const auto &p  = j["phase"];
        l.phaseSource  = p.value("source", PhaseSource::None);
        l.phaseReverse = p.value("reverse", false);
    }

    l.mirror = j.value("mirror", false);

    // null を書いて「カーブ無し＝p*p」を明示できるようにしてある。
    if (j.contains("curve") && !j["curve"].is_null()) l.curve = j["curve"].get<CurveData>();

    l.frequency     = j.value("frequency", 10.0f);
    l.leadThreshold = j.value("leadThreshold", 0.18f);
}

static void to_json(nlohmann::json &j, const FormationAsset &a)
{
    j = nlohmann::json{
        {"name", a.name},
        {"slot", a.slot},
        {"assign",
            {
                {"sideGroups", a.assign.sideGroups},
                {"tierGroups", a.assign.tierGroups},
                {"split", a.assign.split},
                {"rows", a.assign.rows},
                {"fillOrder", a.assign.fillOrder},
            }},
        {"layout",
            {
                {"columnAxis", a.layout.columnAxis},
                {"centerColumns", a.layout.centerColumns},
                {"baseA", a.layout.baseA},
                {"spacingA", a.layout.spacingA},
                {"baseB", a.layout.baseB},
                {"spacingB", a.layout.spacingB},
                {"tierStepB", a.layout.tierStepB},
            }},
        {"layers", a.layers},
        {"origin",
            {
                {"forward", a.origin.forward},
                {"up", a.origin.up},
                {"right", a.origin.right},
            }},
        {"timing", {{"speed", a.speed}}},
        {"output",
            {
                {"look", a.output.look},
                {"chargeMode", a.output.chargeMode},
                {"clampToGround", a.output.clampToGround},
                {"useShiverTelegraph", a.output.useShiverTelegraph},
                {"combat", a.output.combat},
            }},
    };
}

static void from_json(const nlohmann::json &j, FormationAsset &a)
{
    a.name = j.value("name", std::string("Formation"));
    a.slot = j.value("slot", static_cast<uint8_t>(0));

    if (j.contains("assign"))
    {
        const auto &s       = j["assign"];
        a.assign.sideGroups = s.value("sideGroups", 1);
        a.assign.tierGroups = s.value("tierGroups", 1);
        a.assign.split      = s.value("split", AssetSplit::Block);
        a.assign.rows       = s.value("rows", 0);
        a.assign.fillOrder  = s.value("fillOrder", AssetFillOrder::RowFirst);
    }

    if (j.contains("layout"))
    {
        const auto &s          = j["layout"];
        a.layout.columnAxis    = s.value("columnAxis", AssetAxis::Right);
        a.layout.centerColumns = s.value("centerColumns", true);
        a.layout.baseA         = s.value("baseA", 0.0f);
        a.layout.spacingA      = s.value("spacingA", 2.5f);
        a.layout.baseB         = s.value("baseB", 0.0f);
        a.layout.spacingB      = s.value("spacingB", 2.5f);
        a.layout.tierStepB     = s.value("tierStepB", 0.0f);
    }

    a.layers.clear();
    if (j.contains("layers") && j["layers"].is_array())
        a.layers = j["layers"].get<std::vector<FormationLayer>>();

    if (j.contains("origin"))
    {
        const auto &s    = j["origin"];
        a.origin.forward = s.value("forward", 0.0f);
        a.origin.up      = s.value("up", 0.0f);
        a.origin.right   = s.value("right", 0.0f);
    }

    if (j.contains("timing")) a.speed = j["timing"].value("speed", 2.5f);

    if (j.contains("output"))
    {
        const auto &s               = j["output"];
        a.output.look               = s.value("look", LookMode::Auto);
        a.output.chargeMode         = s.value("chargeMode", ChargeMode::RigidFollow);
        a.output.clampToGround      = s.value("clampToGround", false);
        a.output.useShiverTelegraph = s.value("useShiverTelegraph", true);
        if (s.contains("combat")) a.output.combat = s["combat"].get<FormationCombat>();
    }
}

// ---------------------------------------------------------------------------
// 検証
// ---------------------------------------------------------------------------
// 直すのは「そのまま使うと座標が NaN / inf になる値」だけ。1000機を並列で回すので、
// NaN が1つ混ざると全機が消えて、原因のアセットまで辿り着けなくなる。
// 見た目が変なだけの値（間隔0など）は作者の意図かもしれないので触らない。

bool FormationAssetIO::Validate(FormationAsset &asset, const std::string &context)
{
    const char *ctx     = context.empty() ? asset.name.c_str() : context.c_str();
    bool        repaired = false;

    if (asset.assign.sideGroups < 1)
    {
        CCL_LOG_ERROR(LogCategory::AI,
            "[Formation] %s: assign.sideGroups=%d は1未満。割付でゼロ除算になるため 1 に直した",
            ctx, asset.assign.sideGroups);
        asset.assign.sideGroups = 1;
        repaired                = true;
    }
    if (asset.assign.tierGroups < 1)
    {
        CCL_LOG_ERROR(LogCategory::AI,
            "[Formation] %s: assign.tierGroups=%d は1未満。割付でゼロ除算になるため 1 に直した",
            ctx, asset.assign.tierGroups);
        asset.assign.tierGroups = 1;
        repaired                = true;
    }
    if (asset.assign.rows < 0)
    {
        CCL_LOG_ERROR(LogCategory::AI,
            "[Formation] %s: assign.rows=%d が負。自動格子(0)に直した", ctx, asset.assign.rows);
        asset.assign.rows = 0;
        repaired          = true;
    }

    // speed が0以下だと attackProgress が進まず、形が永久に完成しない。
    // 「撃ったのに何も起きない」は原因が最も分かりにくい壊れ方なので必ず直す。
    if (asset.speed <= 0.0f)
    {
        CCL_LOG_ERROR(LogCategory::AI,
            "[Formation] %s: timing.speed=%.3f が0以下。進行度が進まないため 2.5 に直した",
            ctx, asset.speed);
        asset.speed = 2.5f;
        repaired    = true;
    }

    for (size_t i = 0; i < asset.layers.size(); ++i)
    {
        FormationLayer &l = asset.layers[i];

        // 進行度は 0〜1 にクランプされるので、窓がその外にあると永久に発火しない。
        l.windowBegin = std::clamp(l.windowBegin, 0.0f, 1.0f);
        l.windowEnd   = std::clamp(l.windowEnd, 0.0f, 1.0f);

        if (l.windowEnd <= l.windowBegin)
        {
            CCL_LOG_ERROR(LogCategory::AI,
                "[Formation] %s layer[%zu]: window=[%.3f, %.3f] の幅が0以下。[0,1] に直した",
                ctx, i, l.windowBegin, l.windowEnd);
            l.windowBegin = 0.0f;
            l.windowEnd   = 1.0f;
            repaired      = true;
        }

        const float width = l.windowEnd - l.windowBegin;

        if (l.duration <= 0.0f)
        {
            CCL_LOG_ERROR(LogCategory::AI,
                "[Formation] %s layer[%zu]: duration=%.3f が0以下。ゼロ除算になるため窓幅 %.3f に直した",
                ctx, i, l.duration, width);
            l.duration = width;
            repaired   = true;
        }
        else if (l.duration > width)
        {
            // 窓より長いと最後の席が動き切る前に進行度が尽きる（形が途中で止まる）。
            CCL_LOG_ERROR(LogCategory::AI,
                "[Formation] %s layer[%zu]: duration=%.3f が窓幅 %.3f を超えている。窓幅に詰めた",
                ctx, i, l.duration, width);
            l.duration = width;
            repaired   = true;
        }

        if (l.type == LayerType::Shiver)
        {
            if (l.frequency <= 0.0f)
            {
                CCL_LOG_ERROR(LogCategory::AI,
                    "[Formation] %s layer[%zu]: Shiver の frequency=%.3f が0以下。震えないため 10 に直した",
                    ctx, i, l.frequency);
                l.frequency = 10.0f;
                repaired    = true;
            }
            l.leadThreshold = std::clamp(l.leadThreshold, 0.0f, 1.0f);
        }
    }

    return repaired;
}

// ---------------------------------------------------------------------------
// 入出力
// ---------------------------------------------------------------------------

bool FormationAssetIO::Load(const std::string &virtualPath, FormationAsset &outAsset)
{
    const std::string text = FileSystem::ReadAllText(virtualPath);
    if (text.empty())
    {
        CCL_LOG_ERROR(LogCategory::AI, "[Formation] 読み込み失敗: %s（空または存在しない）",
                      virtualPath.c_str());
        return false;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(text);
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::AI, "[Formation] JSON 解析エラー %s: %s",
                      virtualPath.c_str(), e.what());
        return false;
    }

    // 型が想定外（数値が文字列で書かれている等）でも例外が出るので、構築ごと包む。
    try
    {
        outAsset = j.get<FormationAsset>();
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::AI, "[Formation] 構築エラー %s: %s",
                      virtualPath.c_str(), e.what());
        return false;
    }

    Validate(outAsset, virtualPath);
    return true;
}

bool FormationAssetIO::Save(const std::string &virtualPath, const FormationAsset &asset)
{
    // 保存前に直しておくことで、「保存はできたが次に開けない」を作らない。
    FormationAsset checked = asset;
    Validate(checked, virtualPath);

    const nlohmann::json j = checked;
    if (!FileSystem::WriteAllText(virtualPath, j.dump(2)))
    {
        CCL_LOG_ERROR(LogCategory::AI, "[Formation] 保存失敗: %s", virtualPath.c_str());
        return false;
    }
    return true;
}

} // namespace CCL::AI::Formation
