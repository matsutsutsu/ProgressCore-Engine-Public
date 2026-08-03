#include "AnimGraphSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include <fstream>
#include <iomanip> // std::setw 用

bool AnimGraphSerializer::SaveToJSON(const AnimStateGraph &graph, const std::string &filepath)
{
    std::ofstream os(filepath);
    if (!os.is_open())
    {
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Save: cannot open '%s'", filepath.c_str());
        return false;
    }

    try
    {
        json j = graph; // to_json が自動的に呼ばれる
        os << std::setw(4) << j << std::endl;
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Save Error: %s", e.what());
        return false;
    }
    return true;
}

bool AnimGraphSerializer::LoadFromJSON(AnimStateGraph &outGraph, const std::string &filepath, const std::vector<AnimSequence> &availableSequences)
{
    std::ifstream is(filepath);
    if (!is.is_open())
        return false;

    try
    {
        json j;
        is >> j;
        outGraph = j.get<AnimStateGraph>(); // from_json が自動的に呼ばれる
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Load Error: %s", e.what());
        return false;
    }

    // AnimSequence ポインタの紐付け (Binding)
    //   注意: availableSequences の要素アドレスを保持するため、
    //   この vector は呼び出し側でロード後も生存している必要がある。
    //   呼び出し側が空(ResourceManager経由で後から紐付け)の場合は nullptr のまま。
    for (auto &state : outGraph.states)
    {
        state.sequence = nullptr;
        for (const auto &seq : availableSequences)
        {
            if (seq.sequenceName == state.sequenceName)
            {
                state.sequence = &seq;
                break;
            }
        }
    }

    return true;
}