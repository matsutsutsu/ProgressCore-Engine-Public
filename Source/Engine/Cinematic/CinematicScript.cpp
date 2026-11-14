#include "Engine/Cinematic/CinematicScript.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Animation/Core/AnimSequence.h"

#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>

using nlohmann::json;
using nlohmann::ordered_json;

namespace
{
    // --- 読み込み ---

    // [x, y, z] を読む。なければ fallback
    DirectX::XMFLOAT3 ReadVec3(const json &j, const char *key, const DirectX::XMFLOAT3 &fallback)
    {
        if (!j.contains(key) || j[key].size() != 3) return fallback;
        const json &a = j[key];
        return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
    }



    // 列挙値を名前で読む（names は列挙と同じ順）。なければ、または知らない名前なら fallback
    template <class Enum, std::size_t N>
    Enum ReadEnum(const json &j, const char *key, const char *const (&names)[N], Enum fallback)
    {
        const std::string name = j.value(key, std::string());
        for (std::size_t i = 0; i < N; ++i)
        {
            if (name == names[i]) return static_cast<Enum>(i);
        }
        return fallback;
    }



    CameraShot ReadShot(const json &j)
    {
        CameraShot s;
        s.start    = j.value("start", s.start);
        s.duration = j.value("duration", s.duration);
        s.subject  = j.value("subject", s.subject);
        s.motion   = ReadEnum(j, "motion", kCameraMotionNames, s.motion);
        s.ease     = ReadEnum(j, "ease", kCameraEaseNames, s.ease);

        s.lookStart   = ReadVec3(j, "lookStart", s.lookStart);
        s.eyeStart    = ReadVec3(j, "eyeStart", s.eyeStart);
        s.angleStart  = j.value("angleStart", s.angleStart);
        s.radiusStart = j.value("radiusStart", s.radiusStart);
        s.heightStart = j.value("heightStart", s.heightStart);
        s.fovStart    = j.value("fovStart", s.fovStart);

        // 終了を書いていなければ、開始の値のまま動かない
        s.lookEnd   = ReadVec3(j, "lookEnd", s.lookStart);
        s.eyeEnd    = ReadVec3(j, "eyeEnd", s.eyeStart);
        s.angleEnd  = j.value("angleEnd", s.angleStart);
        s.radiusEnd = j.value("radiusEnd", s.radiusStart);
        s.heightEnd = j.value("heightEnd", s.heightStart);
        s.fovEnd    = j.value("fovEnd", s.fovStart);

        s.dof        = j.value("dof", s.dof);
        s.dofRange   = j.value("dofRange", s.dofRange);
        s.dofFalloff = j.value("dofFalloff", s.dofFalloff);
        s.dofMaxBlur = j.value("dofMaxBlur", s.dofMaxBlur);
        return s;
    }



    CinematicSound ReadSound(const json &j)
    {
        CinematicSound s;
        s.time    = j.value("time", s.time);
        s.event   = j.value("event", s.event);
        s.subject = j.value("subject", s.subject);
        s.volume  = j.value("volume", s.volume);
        s.pitch   = j.value("pitch", s.pitch);
        return s;
    }



    CinematicEffect ReadEffect(const json &j)
    {
        CinematicEffect e;
        e.time     = j.value("time", e.time);
        e.effect   = j.value("effect", e.effect);
        e.subject  = j.value("subject", e.subject);
        e.offset   = ReadVec3(j, "offset", e.offset);
        e.scale    = j.value("scale", e.scale);
        e.follow   = j.value("follow", e.follow);
        e.duration = j.value("duration", e.duration);
        return e;
    }



    CinematicSignal ReadSignal(const json &j)
    {
        CinematicSignal s;
        s.time   = j.value("time", s.time);
        s.name   = j.value("name", s.name);
        s.param  = j.value("param", s.param);
        s.param2 = j.value("param2", s.param2);
        s.value  = j.value("value", s.value);
        return s;
    }



    CinematicAnimation ReadAnimation(const json &j)
    {
        CinematicAnimation a;
        a.start    = j.value("start", a.start);
        a.duration = j.value("duration", a.duration);
        a.subject  = j.value("subject", a.subject);
        a.sequence = j.value("sequence", a.sequence);
        a.loop     = j.value("loop", a.loop);
        a.speed    = j.value("speed", a.speed);
        a.blendIn  = j.value("blendIn", a.blendIn);
        a.blendOut = j.value("blendOut", a.blendOut);
        return a;
    }



    // root[key] の配列を read で1つずつ読む。項目がなければ空
    template <class T, class Read>
    std::vector<T> ReadList(const json &root, const char *key, Read read)
    {
        std::vector<T> out;
        if (!root.contains(key)) return out;

        for (const json &item : root[key]) out.push_back(read(item));
        return out;
    }



    // 再生のたびにファイルを読まないよう、アニメのクリップを読んでおく。読めないアニメは外す
    void ResolveAnimations(std::vector<CinematicAnimation> &anims, const std::string &path)
    {
        for (CinematicAnimation &a : anims)
        {
            a.resolved     = a.sequence.empty() ? nullptr : ResourceManager::Instance().LoadAnimSequence(a.sequence.c_str());
            a.clipDuration = a.resolved ? a.resolved->duration : 0.0f;

            if (!a.resolved)
            {
                CCL_LOG_WARN(LogCategory::Game, "[Cinematic] 台本 %s のアニメ '%s' を読めないので外します", path.c_str(),
                             a.sequence.c_str());
            }
        }

        anims.erase(std::remove_if(anims.begin(), anims.end(), [](const CinematicAnimation &a) { return !a.resolved; }),
                    anims.end());
    }



    // --- 書き出し ---

    // 4.4 が 4.400000095367432 と書かれないよう、小数4桁に丸める
    double Num(float v)
    {
        return std::round(static_cast<double>(v) * 10000.0) / 10000.0;
    }



    ordered_json Vec3(const DirectX::XMFLOAT3 &v)
    {
        return ordered_json::array({Num(v.x), Num(v.y), Num(v.z)});
    }



    ordered_json ShotToJson(const CameraShot &s)
    {
        ordered_json j;
        j["start"]       = Num(s.start);
        j["duration"]    = Num(s.duration);
        j["subject"]     = s.subject;
        j["motion"]      = kCameraMotionNames[static_cast<int>(s.motion)];
        j["ease"]        = kCameraEaseNames[static_cast<int>(s.ease)];
        j["lookStart"]   = Vec3(s.lookStart);
        j["lookEnd"]     = Vec3(s.lookEnd);
        j["eyeStart"]    = Vec3(s.eyeStart);
        j["eyeEnd"]      = Vec3(s.eyeEnd);
        j["angleStart"]  = Num(s.angleStart);
        j["angleEnd"]    = Num(s.angleEnd);
        j["radiusStart"] = Num(s.radiusStart);
        j["radiusEnd"]   = Num(s.radiusEnd);
        j["heightStart"] = Num(s.heightStart);
        j["heightEnd"]   = Num(s.heightEnd);
        j["fovStart"]    = Num(s.fovStart);
        j["fovEnd"]      = Num(s.fovEnd);
        j["dof"]         = s.dof;
        j["dofRange"]    = Num(s.dofRange);
        j["dofFalloff"]  = Num(s.dofFalloff);
        j["dofMaxBlur"]  = Num(s.dofMaxBlur);
        return j;
    }



    ordered_json SoundToJson(const CinematicSound &s)
    {
        ordered_json j;
        j["time"]    = Num(s.time);
        j["event"]   = s.event;
        j["subject"] = s.subject;
        j["volume"]  = Num(s.volume);
        j["pitch"]   = Num(s.pitch);
        return j;
    }



    ordered_json EffectToJson(const CinematicEffect &e)
    {
        ordered_json j;
        j["time"]     = Num(e.time);
        j["effect"]   = e.effect;
        j["subject"]  = e.subject;
        j["offset"]   = Vec3(e.offset);
        j["scale"]    = Num(e.scale);
        j["follow"]   = e.follow;
        j["duration"] = Num(e.duration);
        return j;
    }



    ordered_json SignalToJson(const CinematicSignal &s)
    {
        ordered_json j;
        j["time"]   = Num(s.time);
        j["name"]   = s.name;
        j["param"]  = s.param;
        j["param2"] = s.param2;
        j["value"]  = Num(s.value);
        return j;
    }



    ordered_json AnimationToJson(const CinematicAnimation &a)
    {
        ordered_json j;
        j["start"]    = Num(a.start);
        j["duration"] = Num(a.duration); // 0 は「クリップ1回分」のまま書く
        j["subject"]  = a.subject;
        j["sequence"] = a.sequence;
        j["loop"]     = a.loop;
        j["speed"]    = Num(a.speed);
        j["blendIn"]  = Num(a.blendIn);
        j["blendOut"] = Num(a.blendOut);
        return j;
    }



    template <class T, class ToJson>
    ordered_json ListToJson(const std::vector<T> &items, ToJson toJson)
    {
        ordered_json a = ordered_json::array();
        for (const T &item : items) a.push_back(toJson(item));
        return a;
    }
} // namespace



bool CinematicScriptIO::Load(const std::string &path, CinematicScript &out)
{
    const json root = json::parse(FileSystem::ReadAllText(path), nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を読めません（ファイルがないか、JSON が壊れている）",
                      path.c_str());
        return false;
    }

    // 数値の欄に文字列を書いたなど、型が違うと json が例外を投げる
    CinematicScript script;
    script.path = path;
    try
    {
        script.duration   = root.value("duration", script.duration);
        script.allowSkip  = root.value("allowSkip", script.allowSkip);
        script.shots      = ReadList<CameraShot>(root, "shots", ReadShot);
        script.sounds     = ReadList<CinematicSound>(root, "sounds", ReadSound);
        script.effects    = ReadList<CinematicEffect>(root, "effects", ReadEffect);
        script.signals    = ReadList<CinematicSignal>(root, "signals", ReadSignal);
        script.animations = ReadList<CinematicAnimation>(root, "animations", ReadAnimation);
    }
    catch (const json::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を読めません: %s", path.c_str(), e.what());
        return false;
    }

    ResolveAnimations(script.animations, path);
    SortByTime(script);

    out = std::move(script);
    return true;
}



bool CinematicScriptIO::Save(const std::string &path, const CinematicScript &script)
{
    ordered_json root;
    root["duration"]   = Num(script.duration);
    root["allowSkip"]  = script.allowSkip;
    root["shots"]      = ListToJson(script.shots, ShotToJson);
    root["sounds"]     = ListToJson(script.sounds, SoundToJson);
    root["effects"]    = ListToJson(script.effects, EffectToJson);
    root["signals"]    = ListToJson(script.signals, SignalToJson);
    root["animations"] = ListToJson(script.animations, AnimationToJson);

    if (FileSystem::WriteAllText(path, root.dump(2))) return true;

    CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を保存できません", path.c_str());
    return false;
}



void CinematicScriptIO::SortByTime(CinematicScript &script)
{
    auto byStart = [](const auto &a, const auto &b) { return a.start < b.start; };
    auto byTime  = [](const auto &a, const auto &b) { return a.time < b.time; };

    std::stable_sort(script.shots.begin(), script.shots.end(), byStart);
    std::stable_sort(script.sounds.begin(), script.sounds.end(), byTime);
    std::stable_sort(script.effects.begin(), script.effects.end(), byTime);
    std::stable_sort(script.signals.begin(), script.signals.end(), byTime);
    std::stable_sort(script.animations.begin(), script.animations.end(), byStart);
}
