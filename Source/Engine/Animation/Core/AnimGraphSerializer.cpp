#include "AnimGraphSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <fstream>

bool AnimGraphSerializer::SaveToJSON(const AnimStateGraph &graph, const std::string &filepath)
{
    // 「先に全て文字列化し、成功したときだけファイルを開く」順序を必ず守ること。
    //   ofstream は開いた時点で既存ファイルを空にする。先に開いてから to_json が例外を投げると、
    //   catch には入るが元のファイルは既に失われている

    std::string text;
    try
    {
        json j = graph; // to_json が呼ばれる
        text   = j.dump(4);
    }
    catch (const std::exception &e)
    {
        // ここで抜ければ既存ファイルは無傷のまま
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
        // 書き込み中の失敗（ディスク不足など）。この場合はファイルが壊れている
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Save: write failed '%s'", filepath.c_str());
        return false;
    }
    return true;
}

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