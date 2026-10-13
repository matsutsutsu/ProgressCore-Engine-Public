#include "Engine/Cinematic/CinematicScript.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <json.hpp>
#include <algorithm>
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

        // ---------------------------------------------------------
        // 1. 全体の設定
        // ---------------------------------------------------------
        if (!root.is_object())
        {
            err = "ファイル全体を { } で書く";
            return false;
        }

        if (!CheckKeys(root, {"_comment", "duration", "startDelay", "allowSkip", "shots"}, err)) return false;

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
        return true;
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
