#include "AnimGraphSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <fstream>

// グラフを JSON ファイルとして保存する
bool AnimGraphSerializer::SaveToJSON(const AnimStateGraph &graph, const std::string &filepath)
{
    // 先に全て文字列化し、成功したときだけファイルを開く。
    //   ofstream は開いた時点で既存ファイルを空にするので、
    //   先に開くと、to_json が例外を投げたときに元のファイルが失われる
    std::string text;
    try
    {
        json j = graph; // to_json が呼ばれる
        text   = j.dump(4);
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Save Error: %s", e.what());
        return false;
    }

    std::ofstream os(filepath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!os.is_open())
    {
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Save: cannot open '%s'", filepath.c_str());
        return false;
    }

    os.write(text.data(), static_cast<std::streamsize>(text.size()));
    os.flush();
    if (!os)
    {
        // 書き込みの途中で失敗した（ディスク不足など）。
        //   ここまで来るとファイルは壊れている
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Save: write failed '%s'", filepath.c_str());
        return false;
    }
    return true;
}



// JSON ファイルからグラフを読み込む。失敗したら outGraph は変えない
bool AnimGraphSerializer::LoadFromJSON(AnimStateGraph &outGraph, const std::string &filepath)
{
    const std::string text = FileSystem::ReadAllText(filepath);
    if (text.empty())
        return false;

    try
    {
        json j   = json::parse(text);
        outGraph = j.get<AnimStateGraph>(); // from_json が呼ばれる
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Load Error: %s", e.what());
        return false;
    }

    return true;
}
