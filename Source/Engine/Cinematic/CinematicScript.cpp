#include "Engine/Cinematic/CinematicScript.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/Resource/ResourceManager.h" // アニメのトラックのクリップを読む
#include "Engine/Animation/Core/AnimSequence.h"

#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>

using nlohmann::json;

namespace
{
    // --- 項目と値の読み取り ---
    // どれも、項目が無ければ out を書き換えずに true を返す（省略は既定値を使う）。
    // 書き間違いなら err に理由を入れて false を返す。

    /**
     * @brief 知っている項目だけで書かれているかを確かめる。
     * @param j       確かめるオブジェクト。
     * @param allowed 書いてよい項目の名前。
     * @param err     知らない項目があったときの理由。
     * @return 知らない項目が無ければ true。
     */
    bool CheckKeys(const json &j, std::initializer_list<const char *> allowed, std::string &err)
    {
        // 項目名の打ち間違いは、読み飛ばすと既定値で通ってしまい気付けないので、書き間違いとして扱う。
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            const std::string &key = it.key();
            if (std::none_of(allowed.begin(), allowed.end(), [&](const char *name) { return key == name; }))
            {
                err = "知らない項目 \"" + key + "\"";
                return false;
            }
        }
        return true;
    }

    bool ReadFloat(const json &j, const char *key, float &out, std::string &err)
    {
        if (!j.contains(key)) return true;
        if (!j[key].is_number())
        {
            err = std::string("\"") + key + "\" は数値で書く";
            return false;
        }
        out = j[key].get<float>();
        return true;
    }

    bool ReadBool(const json &j, const char *key, bool &out, std::string &err)
    {
        if (!j.contains(key)) return true;
        if (!j[key].is_boolean())
        {
            err = std::string("\"") + key + "\" は true か false で書く";
            return false;
        }
        out = j[key].get<bool>();
        return true;
    }

    bool ReadString(const json &j, const char *key, std::string &out, std::string &err)
    {
        if (!j.contains(key)) return true;
        if (!j[key].is_string())
        {
            err = std::string("\"") + key + "\" は文字列で書く";
            return false;
        }
        out = j[key].get<std::string>();
        return true;
    }

    bool ReadVec3(const json &j, const char *key, DirectX::XMFLOAT3 &out, std::string &err)
    {
        if (!j.contains(key)) return true;
        const json &a = j[key];
        if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number())
        {
            err = std::string("\"") + key + "\" は数値3つの配列 [x, y, z] で書く";
            return false;
        }
        out = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
        return true;
    }

    /**
     * @brief 名前で書いた列挙値を読む。
     * @tparam Enum 読む列挙型。
     * @tparam N    名前の数。
     * @param j     読むオブジェクト。
     * @param key   項目名。
     * @param names 列挙値の名前（列挙の並びと同じ順）。
     * @param out   読めたときだけ書き換える。
     * @param err   書き間違いの理由。
     * @return 項目が無いか、知っている名前なら true。
     */
    template <class Enum, std::size_t N>
    bool ReadEnum(const json &j, const char *key, const char *const (&names)[N], Enum &out, std::string &err)
    {
        std::string name;
        if (!j.contains(key)) return true;
        if (!ReadString(j, key, name, err)) return false;

        for (std::size_t i = 0; i < N; ++i)
        {
            if (name == names[i])
            {
                out = static_cast<Enum>(i);
                return true;
            }
        }
        err = std::string("\"") + key + "\" に知らない名前 \"" + name + "\"";
        return false;
    }

    // --- ショット ---

    /**
     * @brief ショット1つを読む。
     * @param j    ショットのオブジェクト。
     * @param shot 読んだ値を入れる先。
     * @param err  書き間違いの理由。
     * @return 読めたら true。
     */
    bool ParseShot(const json &j, CameraShot &shot, std::string &err)
    {
        if (!j.is_object())
        {
            err = "ショットは { } で書く";
            return false;
        }

        if (!CheckKeys(j,
                       {"_comment", "start", "duration", "subject", "motion", "ease", "blendIn", "lookStart",
                        "lookEnd", "eyeStart", "eyeEnd", "angleStart", "angleEnd", "radiusStart", "radiusEnd",
                        "heightStart", "heightEnd", "fovStart", "fovEnd"},
                       err))
            return false;

        if (!ReadFloat(j, "start", shot.start, err) || !ReadFloat(j, "duration", shot.duration, err) ||
            !ReadString(j, "subject", shot.subject, err) ||
            !ReadEnum(j, "motion", kCameraMotionNames, shot.motion, err) ||
            !ReadEnum(j, "ease", kCameraEaseNames, shot.ease, err) || !ReadFloat(j, "blendIn", shot.blendIn, err))
            return false;

        // 開始の値を先に読み、終了の値の既定を開始と同じにする。
        // 終了を省略したら、その値はショットの間動かない。
        if (!ReadVec3(j, "lookStart", shot.lookStart, err) || !ReadVec3(j, "eyeStart", shot.eyeStart, err) ||
            !ReadFloat(j, "angleStart", shot.angleStart, err) || !ReadFloat(j, "radiusStart", shot.radiusStart, err) ||
            !ReadFloat(j, "heightStart", shot.heightStart, err) || !ReadFloat(j, "fovStart", shot.fovStart, err))
            return false;

        shot.lookEnd   = shot.lookStart;
        shot.eyeEnd    = shot.eyeStart;
        shot.angleEnd  = shot.angleStart;
        shot.radiusEnd = shot.radiusStart;
        shot.heightEnd = shot.heightStart;
        shot.fovEnd    = shot.fovStart;

        if (!ReadVec3(j, "lookEnd", shot.lookEnd, err) || !ReadVec3(j, "eyeEnd", shot.eyeEnd, err) ||
            !ReadFloat(j, "angleEnd", shot.angleEnd, err) || !ReadFloat(j, "radiusEnd", shot.radiusEnd, err) ||
            !ReadFloat(j, "heightEnd", shot.heightEnd, err) || !ReadFloat(j, "fovEnd", shot.fovEnd, err))
            return false;

        // --- 値の範囲 ---
        if (shot.start < 0.0f)
        {
            err = "\"start\" は 0 以上";
            return false;
        }

        // 長さ 0 は進行度の割り算が壊れる（再生側でも弾くが、書き間違いとして先に知らせる）。
        if (shot.duration <= 0.0f)
        {
            err = "\"duration\" は 0 より大きく";
            return false;
        }

        if (shot.blendIn < 0.0f)
        {
            err = "\"blendIn\" は 0 以上";
            return false;
        }

        // 画角は 0 度以下・180 度以上だと投影行列が作れない。
        if (shot.fovStart <= 0.0f || shot.fovStart >= 180.0f || shot.fovEnd <= 0.0f || shot.fovEnd >= 180.0f)
        {
            err = "\"fovStart\" / \"fovEnd\" は 0 より大きく 180 より小さく";
            return false;
        }
        return true;
    }

    // --- カメラ以外のトラック ---

    // 点の時刻は、0 以上で全体の長さ以下にする。
    // 長さを超えた点は、再生位置が届かないので一度も出ない。長さか時刻の書き間違いとして扱う。
    bool CheckPointTime(float time, float duration, std::string &err)
    {
        if (time < 0.0f || time > duration)
        {
            err = "\"time\" は 0 以上、全体の長さ " + std::to_string(duration) + " 秒以下";
            return false;
        }
        return true;
    }

    // 必ず書く文字列が空でないかを確かめる
    bool RequireString(const std::string &value, const char *key, std::string &err)
    {
        if (!value.empty()) return true;
        err = std::string("\"") + key + "\" を書く";
        return false;
    }

    bool ParseSound(const json &j, float duration, CinematicSound &out, std::string &err)
    {
        if (!CheckKeys(j, {"_comment", "time", "event", "subject", "volume", "pitch"}, err)) return false;

        if (!ReadFloat(j, "time", out.time, err) || !ReadString(j, "event", out.event, err) ||
            !ReadString(j, "subject", out.subject, err) || !ReadFloat(j, "volume", out.volume, err) ||
            !ReadFloat(j, "pitch", out.pitch, err))
            return false;

        return CheckPointTime(out.time, duration, err) && RequireString(out.event, "event", err);
    }

    bool ParseEffect(const json &j, float duration, CinematicEffect &out, std::string &err)
    {
        if (!CheckKeys(j, {"_comment", "time", "effect", "subject", "offset", "scale", "follow", "duration"}, err))
            return false;

        if (!ReadFloat(j, "time", out.time, err) || !ReadString(j, "effect", out.effect, err) ||
            !ReadString(j, "subject", out.subject, err) || !ReadVec3(j, "offset", out.offset, err) ||
            !ReadFloat(j, "scale", out.scale, err) || !ReadBool(j, "follow", out.follow, err) ||
            !ReadFloat(j, "duration", out.duration, err))
            return false;

        // エフェクトはアニメの Effect イベントと同じ経路で出すので、基準の Entity が要る。
        return CheckPointTime(out.time, duration, err) && RequireString(out.effect, "effect", err) &&
               RequireString(out.subject, "subject", err);
    }

    bool ParseSignal(const json &j, float duration, CinematicSignal &out, std::string &err)
    {
        if (!CheckKeys(j, {"_comment", "time", "name", "param", "param2", "value"}, err)) return false;

        if (!ReadFloat(j, "time", out.time, err) || !ReadString(j, "name", out.name, err) ||
            !ReadString(j, "param", out.param, err) || !ReadString(j, "param2", out.param2, err) ||
            !ReadFloat(j, "value", out.value, err))
            return false;

        return CheckPointTime(out.time, duration, err) && RequireString(out.name, "name", err);
    }

    bool ParseAnimation(const json &j, float duration, CinematicAnimation &out, std::string &err)
    {
        if (!CheckKeys(j,
                       {"_comment", "start", "duration", "subject", "sequence", "loop", "speed", "blendIn", "blendOut"},
                       err))
            return false;

        if (!ReadFloat(j, "start", out.start, err) || !ReadFloat(j, "duration", out.duration, err) ||
            !ReadString(j, "subject", out.subject, err) || !ReadString(j, "sequence", out.sequence, err) ||
            !ReadBool(j, "loop", out.loop, err) || !ReadFloat(j, "speed", out.speed, err) ||
            !ReadFloat(j, "blendIn", out.blendIn, err) || !ReadFloat(j, "blendOut", out.blendOut, err))
            return false;

        if (!RequireString(out.subject, "subject", err) || !RequireString(out.sequence, "sequence", err)) return false;

        if (out.start < 0.0f || out.start >= duration)
        {
            err = "\"start\" は 0 以上、全体の長さ " + std::to_string(duration) + " 秒より前";
            return false;
        }
        if (out.speed <= 0.0f || out.blendIn < 0.0f || out.blendOut < 0.0f || out.duration < 0.0f)
        {
            err = "\"speed\" は 0 より大きく、\"blendIn\" / \"blendOut\" / \"duration\" は 0 以上";
            return false;
        }

        // ループは終わりが無いので、長さを省略できない。
        if (out.loop && out.duration <= 0.0f)
        {
            err = "\"loop\" が true のときは \"duration\" を書く";
            return false;
        }

        // 再生のたびにファイルを読まないよう、台本を読むときにアニメも読んでおく。
        out.resolved = ResourceManager::Instance().LoadAnimSequence(out.sequence.c_str());
        if (!out.resolved)
        {
            err = "アニメ \"" + out.sequence + "\" を読み込めない";
            return false;
        }

        // 長さの省略（0）は、クリップを1回最後まで再生する長さとみなす（CinematicAnimation::Length）。
        out.clipDuration = out.resolved->duration;
        return true;
    }

    /**
     * @brief 台本のトラック1種類（配列）を読み、時刻の順に並べる。
     * @tparam T     トラックの1要素の型。
     * @tparam Parse 1要素を読む関数。bool(const json &, float, T &, std::string &)。
     * @tparam Time  要素の時刻を返す関数。float(const T &)。
     * @param root     ファイル全体のオブジェクト。
     * @param key      配列の項目名。
     * @param duration 全体の長さ（秒）。
     * @param parse    1要素を読む関数。
     * @param timeOf   要素の時刻を返す関数。
     * @param out      読んだ要素を入れる先。
     * @param err      書き間違いの理由。
     * @return 項目が無いか、すべて読めたら true。
     */
    template <class T, class Parse, class Time>
    bool ParseTrack(const json &root, const char *key, float duration, Parse parse, Time timeOf, std::vector<T> &out,
                    std::string &err)
    {
        // トラックは使わない台本もあるので、省略してよい。
        if (!root.contains(key)) return true;

        if (!root[key].is_array())
        {
            err = std::string("\"") + key + "\" は配列で書く";
            return false;
        }

        const json &items = root[key];
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            T item;
            if (!items[i].is_object())
            {
                err = std::string(key) + "[" + std::to_string(i) + "]: { } で書く";
                return false;
            }
            if (!parse(items[i], duration, item, err))
            {
                err = std::string(key) + "[" + std::to_string(i) + "]: " + err;
                return false;
            }
            out.push_back(std::move(item));
        }

        // 再生側は先頭から順に「時刻を過ぎたか」を見るので、読んだときに並べておく。
        std::stable_sort(out.begin(), out.end(), [&](const T &a, const T &b) { return timeOf(a) < timeOf(b); });
        return true;
    }

    /**
     * @brief 台本全体を読む。
     * @param root   ファイル全体のオブジェクト。
     * @param script 読んだ値を入れる先。
     * @param err    書き間違いの理由。
     * @return 読めたら true。
     */
    bool ParseScript(const json &root, CinematicScript &script, std::string &err)
    {
        // 処理フロー:
        // 1. 全体の設定を読む
        // 2. ショットを1つずつ読む
        // 3. 開始時刻の順に並べ、映らないショットが無いか確かめる
        // 4. カメラ以外のトラック（SE・エフェクト・シグナル・アニメ）を読む

        // ---------------------------------------------------------
        // 1. 全体の設定
        // ---------------------------------------------------------
        if (!root.is_object())
        {
            err = "ファイル全体を { } で書く";
            return false;
        }

        if (!CheckKeys(root,
                       {"_comment", "duration", "startDelay", "allowSkip", "shots", "sounds", "effects", "signals",
                        "animations"},
                       err))
            return false;

        if (!ReadFloat(root, "duration", script.duration, err) ||
            !ReadFloat(root, "startDelay", script.startDelay, err) ||
            !ReadBool(root, "allowSkip", script.allowSkip, err))
            return false;

        if (script.duration <= 0.0f)
        {
            err = "\"duration\" は 0 より大きく";
            return false;
        }
        if (script.startDelay < 0.0f)
        {
            err = "\"startDelay\" は 0 以上";
            return false;
        }

        // ---------------------------------------------------------
        // 2. ショット
        // ---------------------------------------------------------

        // ショットの無い台本は、再生しても映すものが無い。
        if (!root.contains("shots") || !root["shots"].is_array() || root["shots"].empty())
        {
            err = "\"shots\" にショットを1つ以上、配列で書く";
            return false;
        }

        const json &shots = root["shots"];
        for (std::size_t i = 0; i < shots.size(); ++i)
        {
            CameraShot shot;
            if (!ParseShot(shots[i], shot, err))
            {
                err = "shots[" + std::to_string(i) + "]: " + err;
                return false;
            }
            script.shots.push_back(std::move(shot));
        }

        // ---------------------------------------------------------
        // 3. 並び順と映らないショット
        // ---------------------------------------------------------

        // ショットの探し方は開始時刻の昇順を前提にしているので、読んだときに並べておく。
        // 同じ開始時刻のショットは、書いた順を保つ。
        std::stable_sort(script.shots.begin(), script.shots.end(),
                         [](const CameraShot &a, const CameraShot &b) { return a.start < b.start; });

        // 全体の長さ以降に始まるショットは、一度も映らない。長さか開始時刻の書き間違いとして扱う。
        if (script.shots.back().start >= script.duration)
        {
            err = "開始時刻 " + std::to_string(script.shots.back().start) + " 秒のショットが、全体の長さ " +
                  std::to_string(script.duration) + " 秒以降に始まるので映らない";
            return false;
        }

        // ---------------------------------------------------------
        // 4. カメラ以外のトラック
        // ---------------------------------------------------------
        return ParseTrack(root, "sounds", script.duration, ParseSound,
                          [](const CinematicSound &s) { return s.time; }, script.sounds, err) &&
               ParseTrack(root, "effects", script.duration, ParseEffect,
                          [](const CinematicEffect &e) { return e.time; }, script.effects, err) &&
               ParseTrack(root, "signals", script.duration, ParseSignal,
                          [](const CinematicSignal &s) { return s.time; }, script.signals, err) &&
               ParseTrack(root, "animations", script.duration, ParseAnimation,
                          [](const CinematicAnimation &a) { return a.start; }, script.animations, err);
    }

    // --- 書き出し ---
    // 項目の並びを書いた順に保つため ordered_json を使う（json はキーの名前順に並べ替える）。

    using ordered_json = nlohmann::ordered_json;

    // float をそのまま double にすると 4.4 が 4.400000095367432 と書かれるので、小数4桁に丸める。
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
        j["blendIn"]     = Num(s.blendIn);
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

    /**
     * @brief 要素の列を JSON の配列にする。
     * @param items  書く要素の列。
     * @param toJson 1要素を JSON にする関数。
     */
    template <class T, class ToJson>
    ordered_json ArrayToJson(const std::vector<T> &items, ToJson toJson)
    {
        ordered_json a = ordered_json::array();
        for (const T &item : items) a.push_back(toJson(item));
        return a;
    }

    ordered_json ScriptToJson(const CinematicScript &script)
    {
        ordered_json root;
        root["duration"]   = Num(script.duration);
        root["startDelay"] = Num(script.startDelay);
        root["allowSkip"]  = script.allowSkip;
        root["shots"]      = ArrayToJson(script.shots, ShotToJson);
        root["sounds"]     = ArrayToJson(script.sounds, SoundToJson);
        root["effects"]    = ArrayToJson(script.effects, EffectToJson);
        root["signals"]    = ArrayToJson(script.signals, SignalToJson);
        root["animations"] = ArrayToJson(script.animations, AnimationToJson);
        return root;
    }
} // namespace

bool CinematicScriptIO::Load(const std::string &path, CinematicScript &out)
{
    // ファイルを丸ごと読み込む。空ならファイルが無いか空
    const std::string text = FileSystem::ReadAllText(path);
    if (text.empty())
    {
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を読み込めません: ファイルが無いか空", path.c_str());
        return false;
    }

    // 例外を投げさせず、壊れた JSON は discarded として受け取る。
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded())
    {
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を読み込めません: JSON の書き方が正しくない",
                      path.c_str());
        return false;
    }

    CinematicScript script;
    script.path = path;

    std::string err;
    if (!ParseScript(root, script, err))
    {
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を読み込めません: %s", path.c_str(), err.c_str());
        return false;
    }

    out = std::move(script);
    return true;
}

bool CinematicScriptIO::Save(const std::string &path, const CinematicScript &script, std::string &err)
{
    // 処理フロー:
    // 1. JSON の文字列にする
    // 2. 読み込みと同じ決まりで確かめる
    // 3. ファイルへ書く

    // ---------------------------------------------------------
    // 1. JSON の文字列
    // ---------------------------------------------------------
    const std::string text = ScriptToJson(script).dump(2);

    // ---------------------------------------------------------
    // 2. 読み込みと同じ決まりで確かめる
    // ---------------------------------------------------------

    // 書いた文字列をそのまま読み直して確かめる。
    // 決まりを書き出し側にもう一度書くと、読み込み側だけ直したときに食い違う。
    CinematicScript check;
    if (!ParseScript(json::parse(text, nullptr, false), check, err))
    {
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を保存しません: %s", path.c_str(), err.c_str());
        return false;
    }

    // ---------------------------------------------------------
    // 3. ファイルへ書く
    // ---------------------------------------------------------
    if (!FileSystem::WriteAllText(path, text))
    {
        err = "ファイルに書けない";
        CCL_LOG_ERROR(LogCategory::Game, "[Cinematic] 台本 %s を保存できません: %s", path.c_str(), err.c_str());
        return false;
    }
    return true;
}
