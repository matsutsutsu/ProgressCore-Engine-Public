// 役割・設計方針は TimelineAsset.h を参照。
#include "Engine/Timeline/TimelineAsset.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/Interpolation.h"          // SmoothStep()
#include "Engine/Graphics/Resource/ResourceManager.h" // Animation クリップの AnimSequence を引く
#include "Engine/Animation/Core/AnimSequence.h"

#include <json.hpp>
#include <algorithm>
#include <cmath>

using DirectX::SimpleMath::Vector3;
using nlohmann::json;

namespace
{
    // ========================================================================
    // 読み込み
    // ========================================================================

    // [x, y, z] の配列を読む。無い・形が違うときは既定値のまま返す
    Vector3 ReadVec3(const json &j, const char *key, const Vector3 &fallback)
    {
        if (!j.contains(key)) return fallback;
        const auto &a = j[key];
        if (!a.is_array() || a.size() != 3) return fallback;
        return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
    }

    // 文字列の列挙値を読む。知らない名前は既定値のまま（打ち間違いで落ちないように）
    template <class Enum, std::size_t N>
    Enum ReadEnum(const json &j, const char *key, const char *const (&names)[N], Enum fallback)
    {
        if (!j.contains(key) || !j[key].is_string()) return fallback;
        const std::string s = j[key].get<std::string>();
        for (std::size_t i = 0; i < N; ++i)
        {
            if (s == names[i]) return static_cast<Enum>(i);
        }
        return fallback;
    }

    CameraShotClip ParseCameraShot(const json &j)
    {
        CameraShotClip c;
        c.comment = j.value("_comment", c.comment);

        c.start    = j.value("start", c.start);
        c.duration = (std::max)(j.value("duration", c.duration), 0.01f); // 0 秒は進行度の割り算が壊れる

        c.subject     = j.value("subject", c.subject);
        c.lookSubject = j.value("lookSubject", c.lookSubject);

        c.space         = ReadEnum(j, "space", kCameraShotSpaceNames, c.space);
        c.followSubject = j.value("followSubject", c.followSubject);
        c.motion        = ReadEnum(j, "motion", kCameraShotMotionNames, c.motion);

        c.lookStart = ReadVec3(j, "lookStart", c.lookStart);
        c.lookEnd   = ReadVec3(j, "lookEnd", c.lookStart); // 終了を省略したら動かさない
        c.eyeStart  = ReadVec3(j, "eyeStart", c.eyeStart);
        c.eyeEnd    = ReadVec3(j, "eyeEnd", c.eyeStart);

        c.angleStart  = j.value("angleStart", c.angleStart);
        c.angleEnd    = j.value("angleEnd", c.angleStart);
        c.radiusStart = j.value("radiusStart", c.radiusStart);
        c.radiusEnd   = j.value("radiusEnd", c.radiusStart);
        c.heightStart = j.value("heightStart", c.heightStart);
        c.heightEnd   = j.value("heightEnd", c.heightStart);

        c.fovStart = j.value("fovStart", c.fovStart);
        c.fovEnd   = j.value("fovEnd", c.fovStart);

        c.ease = ReadEnum(j, "ease", kTimelineEaseNames, c.ease);

        c.transitionIn = ReadEnum(j, "transitionIn", kCameraShotTransitionNames, c.transitionIn);
        c.blendTime    = (std::max)(j.value("blendTime", c.blendTime), 0.0f);
        return c;
    }

    SoundClip ParseSound(const json &j)
    {
        SoundClip c;
        c.comment    = j.value("_comment", c.comment);
        c.time       = (std::max)(j.value("time", c.time), 0.0f);
        c.event      = j.value("event", c.event);
        c.spatial    = j.value("spatial", c.spatial);
        c.subject    = j.value("subject", c.subject);
        c.volume     = j.value("volume", c.volume);
        c.pitch      = j.value("pitch", c.pitch);
        c.fireOnSkip = j.value("fireOnSkip", c.fireOnSkip);
        return c;
    }

    EffectClip ParseEffect(const json &j)
    {
        EffectClip c;
        c.comment    = j.value("_comment", c.comment);
        c.time       = (std::max)(j.value("time", c.time), 0.0f);
        c.duration   = (std::max)(j.value("duration", c.duration), 0.0f);
        c.effect     = j.value("effect", c.effect);
        c.subject    = j.value("subject", c.subject);
        c.attachTag  = j.value("attachTag", c.attachTag);
        c.offset     = ReadVec3(j, "offset", c.offset);
        c.scale      = j.value("scale", c.scale);
        c.follow     = j.value("follow", c.follow);
        c.fireOnSkip = j.value("fireOnSkip", c.fireOnSkip);
        return c;
    }

    SignalClip ParseSignal(const json &j)
    {
        SignalClip c;
        c.comment    = j.value("_comment", c.comment);
        c.time       = (std::max)(j.value("time", c.time), 0.0f);
        c.name       = j.value("name", c.name);
        c.param      = j.value("param", c.param);
        c.param2     = j.value("param2", c.param2);
        c.value      = j.value("value", c.value);
        c.fireOnSkip = j.value("fireOnSkip", c.fireOnSkip);
        return c;
    }

    AnimationClip ParseAnimation(const json &j)
    {
        AnimationClip c;
        c.comment  = j.value("_comment", c.comment);
        c.start    = (std::max)(j.value("start", c.start), 0.0f);
        c.duration = (std::max)(j.value("duration", c.duration), 0.0f);
        c.subject  = j.value("subject", c.subject);
        c.sequence = j.value("sequence", c.sequence);
        c.loop     = j.value("loop", c.loop);
        c.speed    = (std::max)(j.value("speed", c.speed), 0.01f); // 0 だと長さの自動計算が割り算で壊れる
        c.blendIn  = (std::max)(j.value("blendIn", c.blendIn), 0.0f);
        c.blendOut = (std::max)(j.value("blendOut", c.blendOut), 0.0f);
        return c;
    }

    // ========================================================================
    // 書き出し
    //   既定値と同じでも全部書く。省略すると、既定値を変えたときに保存済みの資産の意味が黙って変わる。
    // ========================================================================

    json Vec3Json(const Vector3 &v) { return json::array({v.x, v.y, v.z}); }

    // _comment は空なら書かない（JSON を手で読むときの雑音を増やさないため）
    void WriteComment(json &j, const std::string &comment)
    {
        if (!comment.empty()) j["_comment"] = comment;
    }

    json CameraShotJson(const CameraShotClip &c)
    {
        json j = json::object();
        WriteComment(j, c.comment);
        j["start"]         = c.start;
        j["duration"]      = c.duration;
        j["subject"]       = c.subject;
        j["lookSubject"]   = c.lookSubject;
        j["space"]         = kCameraShotSpaceNames[static_cast<int>(c.space)];
        j["followSubject"] = c.followSubject;
        j["motion"]        = kCameraShotMotionNames[static_cast<int>(c.motion)];
        j["lookStart"]     = Vec3Json(c.lookStart);
        j["lookEnd"]       = Vec3Json(c.lookEnd);
        j["eyeStart"]      = Vec3Json(c.eyeStart);
        j["eyeEnd"]        = Vec3Json(c.eyeEnd);
        j["angleStart"]    = c.angleStart;
        j["angleEnd"]      = c.angleEnd;
        j["radiusStart"]   = c.radiusStart;
        j["radiusEnd"]     = c.radiusEnd;
        j["heightStart"]   = c.heightStart;
        j["heightEnd"]     = c.heightEnd;
        j["fovStart"]      = c.fovStart;
        j["fovEnd"]        = c.fovEnd;
        j["ease"]          = kTimelineEaseNames[static_cast<int>(c.ease)];
        j["transitionIn"]  = kCameraShotTransitionNames[static_cast<int>(c.transitionIn)];
        j["blendTime"]     = c.blendTime;
        return j;
    }

    json SoundJson(const SoundClip &c)
    {
        json j = json::object();
        WriteComment(j, c.comment);
        j["time"]       = c.time;
        j["event"]      = c.event;
        j["spatial"]    = c.spatial;
        j["subject"]    = c.subject;
        j["volume"]     = c.volume;
        j["pitch"]      = c.pitch;
        j["fireOnSkip"] = c.fireOnSkip;
        return j;
    }

    json EffectJson(const EffectClip &c)
    {
        json j = json::object();
        WriteComment(j, c.comment);
        j["time"]       = c.time;
        j["duration"]   = c.duration;
        j["effect"]     = c.effect;
        j["subject"]    = c.subject;
        j["attachTag"]  = c.attachTag;
        j["offset"]     = Vec3Json(c.offset);
        j["scale"]      = c.scale;
        j["follow"]     = c.follow;
        j["fireOnSkip"] = c.fireOnSkip;
        return j;
    }

    json SignalJson(const SignalClip &c)
    {
        json j = json::object();
        WriteComment(j, c.comment);
        j["time"]       = c.time;
        j["name"]       = c.name;
        j["param"]      = c.param;
        j["param2"]     = c.param2;
        j["value"]      = c.value;
        j["fireOnSkip"] = c.fireOnSkip;
        return j;
    }

    json AnimationJson(const AnimationClip &c)
    {
        json j = json::object();
        WriteComment(j, c.comment);
        j["start"]    = c.start;
        j["duration"] = c.duration;
        j["subject"]  = c.subject;
        j["sequence"] = c.sequence;
        j["loop"]     = c.loop;
        j["speed"]    = c.speed;
        j["blendIn"]  = c.blendIn;
        j["blendOut"] = c.blendOut;
        return j;
    }

    // 小数を4桁に丸める（中身を再帰的にたどる）。
    //   float を double で書くと 2.8 が 2.799999952316284 になり、手で読む・差分を見るときに邪魔になる。
    //   1/10000 秒・1/10000 m は演出の調整に十分細かい
    void RoundFloats(json &j)
    {
        if (j.is_number_float())
        {
            j = std::round(j.get<double>() * 10000.0) / 10000.0;
            return;
        }
        if (j.is_array() || j.is_object())
        {
            for (auto &child : j)
                RoundFloats(child);
        }
    }

    // 1種類のトラックを書く。クリップが無ければトラックごと書かない
    template <class Clip, class ToJson>
    void WriteTrack(json &tracks, const char *type, const std::vector<Clip> &clips, ToJson toJson)
    {
        if (clips.empty()) return;
        json track      = json::object();
        track["type"]   = type;
        track["clips"]  = json::array();
        for (const auto &c : clips)
            track["clips"].push_back(toJson(c));
        tracks.push_back(std::move(track));
    }
} // namespace

std::vector<std::string> TimelineAsset::CollectRoles() const
{
    std::vector<std::string> roles;
    auto add = [&](const std::string &r) {
        if (r.empty()) return;
        if (std::find(roles.begin(), roles.end(), r) == roles.end()) roles.push_back(r);
    };
    for (const auto &c : cameraShots)
    {
        add(c.subject);
        add(c.lookSubject);
    }
    for (const auto &c : sounds)
    {
        if (c.spatial) add(c.subject); // 2D の音は位置を使わないので、役割が無くても鳴らせる
    }
    for (const auto &c : effects)
        add(c.subject);
    for (const auto &c : animations)
        add(c.subject);
    return roles;
}

bool TimelineAssetLoader::Load(const std::string &path, TimelineAsset &out, std::string &errMsg)
{
    const std::string text = FileSystem::ReadAllText(path);
    if (text.empty())
    {
        errMsg = "file not found or empty";
        return false;
    }

    try
    {
        const auto j = json::parse(text);

        out         = TimelineAsset{};
        out.path    = path;
        out.comment = j.value("_comment", std::string{});

        if (j.contains("tracks") && j["tracks"].is_array())
        {
            for (const auto &track : j["tracks"])
            {
                const std::string type = track.value("type", std::string{});
                if (!track.contains("clips") || !track["clips"].is_array()) continue;

                // 同じ種類のトラックが複数あってもクリップは1列にまとめる。
                // 時刻で並べ直すので、トラックを分けるのは JSON を読みやすくするためだけでよい。
                for (const auto &clip : track["clips"])
                {
                    if (type == "Camera") out.cameraShots.push_back(ParseCameraShot(clip));
                    else if (type == "Sound") out.sounds.push_back(ParseSound(clip));
                    else if (type == "Effect") out.effects.push_back(ParseEffect(clip));
                    else if (type == "Animation") out.animations.push_back(ParseAnimation(clip));
                    else if (type == "Signal") out.signals.push_back(ParseSignal(clip));
                    // 知らない種類は読み飛ばす（新しいトラックを足した資産を古い実行ファイルで開いても落ちないように）
                }
            }
        }

        out.durationExplicit = j.contains("duration");
        if (out.durationExplicit) out.duration = j["duration"].get<float>();
        Normalize(out);

        // アニメだけのタイムラインは、長さがクリップの長さの解決後に決まるので 0 でも通す
        if (out.duration <= 0.0f && out.animations.empty())
        {
            errMsg = "duration is 0 (no clips?)";
            return false;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        errMsg = e.what();
        return false;
    }
}

bool TimelineAssetLoader::Save(const std::string &path, const TimelineAsset &asset, std::string &errMsg)
{
    json j = json::object();
    WriteComment(j, asset.comment);
    // 明示していないなら書かない。書くと、次に読んだとき「明示した」扱いになり、
    // クリップを伸ばしても全体の長さが付いてこなくなる
    if (asset.durationExplicit) j["duration"] = asset.duration;

    json tracks = json::array();
    WriteTrack(tracks, "Camera", asset.cameraShots, CameraShotJson);
    WriteTrack(tracks, "Sound", asset.sounds, SoundJson);
    WriteTrack(tracks, "Effect", asset.effects, EffectJson);
    WriteTrack(tracks, "Animation", asset.animations, AnimationJson);
    WriteTrack(tracks, "Signal", asset.signals, SignalJson);
    j["tracks"] = std::move(tracks);
    RoundFloats(j);

    // 手で読む・差分を見ることがあるので字下げして書く。日本語のコメントはそのまま残す
    if (!FileSystem::WriteAllText(path, j.dump(4, ' ', false) + "\n"))
    {
        errMsg = "write failed";
        return false;
    }
    return true;
}

void TimelineAssetLoader::Normalize(TimelineAsset &asset)
{
    // ショットの検索は「start が t 以下の最後のもの」、点イベントの発火は「先頭から順に」
    // なので、どれも時刻の昇順に揃えておく
    std::stable_sort(asset.cameraShots.begin(), asset.cameraShots.end(),
                     [](const CameraShotClip &a, const CameraShotClip &b) { return a.start < b.start; });
    std::stable_sort(asset.sounds.begin(), asset.sounds.end(),
                     [](const SoundClip &a, const SoundClip &b) { return a.time < b.time; });
    std::stable_sort(asset.effects.begin(), asset.effects.end(),
                     [](const EffectClip &a, const EffectClip &b) { return a.time < b.time; });
    std::stable_sort(asset.signals.begin(), asset.signals.end(),
                     [](const SignalClip &a, const SignalClip &b) { return a.time < b.time; });
    std::stable_sort(asset.animations.begin(), asset.animations.end(),
                     [](const AnimationClip &a, const AnimationClip &b) { return a.start < b.start; });

    if (asset.durationExplicit) return;

    float lastEnd = 0.0f;
    for (const auto &c : asset.cameraShots)
        lastEnd = (std::max)(lastEnd, c.End());
    for (const auto &c : asset.sounds)
        lastEnd = (std::max)(lastEnd, c.time);
    for (const auto &c : asset.effects)
        lastEnd = (std::max)(lastEnd, c.time);
    for (const auto &c : asset.signals)
        lastEnd = (std::max)(lastEnd, c.time);
    // 長さ 0（=クリップの長さ）のアニメはまだ start しか分からない。ResolveAnimations の後で呼び直すと伸びる
    for (const auto &c : asset.animations)
        lastEnd = (std::max)(lastEnd, c.End());
    asset.duration = lastEnd;
}

void TimelineAssetLoader::ResolveAnimations(TimelineAsset &asset)
{
    auto &rm = ResourceManager::Instance();
    for (auto &clip : asset.animations)
    {
        clip.resolved = nullptr;
        if (clip.sequence.empty()) continue;

        clip.resolved = rm.LoadAnimSequence(clip.sequence.c_str());
        if (!clip.resolved)
        {
            CCL_LOG_ERROR(LogCategory::Game, "[Timeline] アニメの読み込みに失敗（このクリップは再生しない）: %s",
                          clip.sequence.c_str());
            continue;
        }

        // 長さの省略は「クリップを1回最後まで」。ループは終わりが無いので本来は明示が要る
        if (clip.duration <= 0.0f)
        {
            if (clip.loop)
                CCL_LOG_WARN(LogCategory::Game, "[Timeline] ループのアニメに duration が無いので1周分にします: %s",
                             clip.sequence.c_str());
            clip.duration = clip.resolved->duration / clip.speed;
        }
    }
}

float TimelineAssetLoader::ApplyEase(float t, TimelineEase ease)
{
    t = std::clamp(t, 0.0f, 1.0f);
    switch (ease)
    {
    case TimelineEase::EaseIn:
        return t * t;
    case TimelineEase::EaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case TimelineEase::EaseInOut:
        return CCL::Math::SmoothStep(t);
    case TimelineEase::Linear:
    default:
        return t;
    }
}
