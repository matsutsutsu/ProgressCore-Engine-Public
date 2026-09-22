// 陣形アセットの JSON 変換と検証の実装。
//
// 項目の読み書きは FormationAssetMeta.h の表を回すだけで、ここに項目名は出てこない。
// 表に載らない2つ（layers の配列と、各層の curve）だけを手で扱う。
//
// enum は数値ではなく名前で保存する。数値だと "columnAxis": 2 と出て、意味を知るのに
// 毎回ヘッダを引くことになり、手で読み書きできるアセットという前提が崩れる。
#include "FormationAssetIO.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Game/Logics/AI/Drone/Formation/FormationAssetMeta.h"

#include <json.hpp>

#include <algorithm>
#include <charconv>
#include <exception>
#include <string>

namespace CCL::AI::Formation
{

    // ---------------------------------------------------------------------------
    // 表 1つぶんの読み書き
    // ---------------------------------------------------------------------------

    void FormationAssetIO::WriteFields(nlohmann::json &j, const std::vector<FieldDescriptor> &fields, const void *obj)
    {
        for (const FieldDescriptor &fd : fields)
        {
            if (fd.kind == FieldKind::EnumU8)
            {
                const uint8_t v = RawField<const uint8_t>(obj, fd.offset);
                if (v < fd.enumCount)
                {
                    j[fd.jsonName] = fd.enumNames[v];
                    continue;
                }
            }
            if (fd.kind == FieldKind::Float)
            {
                j[fd.jsonName] = ShortestDouble(RawField<const float>(obj, fd.offset));
                continue;
            }
            ComponentMetaJson::SerializeField(j, fd, obj, nullptr);
        }
    }

    void FormationAssetIO::ReadFields(const nlohmann::json &j, const std::vector<FieldDescriptor> &fields, void *obj)
    {
        for (const FieldDescriptor &fd : fields)
        {
            if (fd.kind == FieldKind::EnumU8 && j.contains(fd.jsonName) && j.at(fd.jsonName).is_string())
            {
                const std::string  name  = j.at(fd.jsonName).get<std::string>();
                const char *const *first = fd.enumNames;
                const char *const *last  = fd.enumNames + fd.enumCount;
                const auto         it    = std::find_if(first, last, [&](const char *n) { return name == n; });

                if (it != last)
                    RawField<uint8_t>(obj, fd.offset) = static_cast<uint8_t>(it - first);
                else
                    CCL_LOG_ERROR(LogCategory::AI, "[Formation] %s=\"%s\" は知らない値。既定値のまま読んだ",
                                  fd.jsonName, name.c_str());
                continue;
            }
            ComponentMetaJson::DeserializeField(j, fd, obj, nullptr);
        }
    }

    double FormationAssetIO::ShortestDouble(float v)
    {
        char       buf[32];
        const auto end = std::to_chars(buf, buf + sizeof(buf), v).ptr;
        return std::stod(std::string(buf, end));
    }

    // ---------------------------------------------------------------------------
    // アセット全体の組み立て
    // ---------------------------------------------------------------------------

    nlohmann::json FormationAssetIO::ToJson(const FormationAsset &a)
    {
        nlohmann::json j;
        WriteFields(j, Meta::RootFields(), &a);
        WriteFields(j["assign"], Meta::AssignFields(), &a.assign);
        WriteFields(j["layout"], Meta::LayoutFields(), &a.layout);

        j["layers"] = nlohmann::json::array();
        for (const FormationLayer &l : a.layers)
        {
            nlohmann::json jl;
            WriteFields(jl, Meta::LayerFields(), &l);

            // Shiver でしか意味を持たない値を他の層に書くと、「何の周波数だ」と読まれる。
            if (l.type != LayerType::Shiver)
            {
                jl.erase("frequency");
                jl.erase("leadThreshold");
            }
            // キーが無いカーブ＝既定の easeIn。空の CurveData を毎層に書くと JSON が膨らむ。
            if (!l.curve.empty())
            {
                jl["curve"] = l.curve;
                nlohmann::json &keys = jl["curve"]["keys"];
                for (size_t i = 0; i < l.curve.keys.size(); ++i)
                {
                    const CurveKeyframe &k = l.curve.keys[i];
                    keys[i]["time"]        = ShortestDouble(k.time);
                    keys[i]["value"]       = ShortestDouble(k.value);
                    keys[i]["inTangent"]   = ShortestDouble(k.inTangent);
                    keys[i]["outTangent"]  = ShortestDouble(k.outTangent);
                }
            }

            j["layers"].push_back(std::move(jl));
        }

        WriteFields(j["origin"], Meta::OriginFields(), &a.origin);
        WriteFields(j["output"], Meta::OutputFields(), &a.output);
        WriteFields(j["output"]["combat"], Meta::CombatFields(), &a.output.combat);
        return j;
    }

    void FormationAssetIO::FromJson(const nlohmann::json &j, FormationAsset &a)
    {
        // 入れ子が丸ごと無いファイルも読めるよう、無ければ空オブジェクト（＝全項目既定値）で回す。
        const nlohmann::json none = nlohmann::json::object();

        ReadFields(j, Meta::RootFields(), &a);
        ReadFields(j.value("assign", none), Meta::AssignFields(), &a.assign);
        ReadFields(j.value("layout", none), Meta::LayoutFields(), &a.layout);

        a.layers.clear();
        for (const nlohmann::json &jl : j.value("layers", nlohmann::json::array()))
        {
            FormationLayer &l = a.layers.emplace_back();
            ReadFields(jl, Meta::LayerFields(), &l);

            // null を書いて「カーブ無し＝p*p」を明示できるようにしてある。
            if (jl.contains("curve") && !jl.at("curve").is_null()) l.curve = jl.at("curve").get<CurveData>();
        }

        ReadFields(j.value("origin", none), Meta::OriginFields(), &a.origin);

        const nlohmann::json output = j.value("output", none);
        ReadFields(output, Meta::OutputFields(), &a.output);
        ReadFields(output.value("combat", none), Meta::CombatFields(), &a.output.combat);
    }

    // ---------------------------------------------------------------------------
    // 検証
    // ---------------------------------------------------------------------------
    // 直すのは「そのまま使うと座標が NaN / inf になる値」だけ。1000機を並列で回すので、
    // NaN が1つ混ざると全機が消えて、原因のアセットまで辿り着けなくなる。
    // 見た目が変なだけの値（間隔0など）は作者の意図かもしれないので触らない。

    bool FormationAssetIO::Validate(FormationAsset &asset, const std::string &context)
    {
        const char *ctx      = context.empty() ? asset.name.c_str() : context.c_str();
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

        // 0以下だと attackProgress が進まず、形が永久に完成しない。
        // 「撃ったのに何も起きない」は原因が最も分かりにくい壊れ方なので必ず直す。
        if (asset.attackSeconds <= 0.0f)
        {
            CCL_LOG_ERROR(LogCategory::AI,
                          "[Formation] %s: attackSeconds=%.3f が0以下。進行度が進まないため 0.4 に直した",
                          ctx, asset.attackSeconds);
            asset.attackSeconds = 0.4f;
            repaired            = true;
        }

        // Shiver の時間帯を「予告の何割か」に直すときの割る数。0以下だと震えの位置が inf になる。
        if (asset.telegraphSeconds <= 0.0f)
        {
            CCL_LOG_ERROR(LogCategory::AI,
                          "[Formation] %s: telegraphSeconds=%.3f が0以下。震えの位置が決まらないため 1.0 に直した",
                          ctx, asset.telegraphSeconds);
            asset.telegraphSeconds = 1.0f;
            repaired               = true;
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

        // 構文エラーも、型が想定外（数値が文字列で書かれている等）も例外で来るので、まとめて包む。
        try
        {
            FormationAsset asset;
            FromJson(nlohmann::json::parse(text), asset);
            outAsset = std::move(asset);
        }
        catch (const std::exception &e)
        {
            CCL_LOG_ERROR(LogCategory::AI, "[Formation] 読み込みエラー %s: %s", virtualPath.c_str(), e.what());
            return false;
        }

        // 保存先を覚えさせるのは読み込み側の仕事（JSON にはパスを書かない）。
        outAsset.sourcePath = virtualPath;

        Validate(outAsset, virtualPath);
        return true;
    }

    bool FormationAssetIO::Save(const std::string &virtualPath, const FormationAsset &asset)
    {
        // 保存前に直しておくことで、「保存はできたが次に開けない」を作らない。
        FormationAsset checked = asset;
        Validate(checked, virtualPath);

        if (!FileSystem::WriteAllText(virtualPath, ToJson(checked).dump(2)))
        {
            CCL_LOG_ERROR(LogCategory::AI, "[Formation] 保存失敗: %s", virtualPath.c_str());
            return false;
        }
        return true;
    }

} // namespace CCL::AI::Formation
