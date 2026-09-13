#include "AnimGraphSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include <fstream>
#include <iomanip> // std::setw 用

// グラフを JSON ファイルへ書き出す。
//
// 「先に全部を文字列化し、成功したときだけファイルを開く」順序を必ず守ること。
//   ofstream はコンストラクタで開いた瞬間に既存ファイルを空にする。以前は
//   os を開いてから to_json を走らせていたため、シリアライズが投げると
//   catch には入るものの【元のファイルは既に失われている】状態だった。
//   手で組んだアニメグラフが一瞬で消える経路なので、順序を逆にしてある。
//   （BT・シーケンサー・カーブエディタは元からこの順序）
bool AnimGraphSerializer::SaveToJSON(const AnimStateGraph &graph, const std::string &filepath)
{
    std::string text;
    try
    {
        json j = graph; // to_json が自動的に呼ばれる
        text   = j.dump(4);
    }
    catch (const std::exception &e)
    {
        // ここで抜ければ既存ファイルは無傷のまま。
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
        // 書き込み中の失敗（ディスク不足など）。ここまで来るとファイルは壊れている。
        CCL_LOG_ERROR(LogCategory::Editor, "JSON Save: write failed '%s'", filepath.c_str());
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
    BindSequencesRecursive(outGraph, availableSequences);

    return true;
}

// サブマシンの子グラフまで再帰して束縛する。
//   トップ階層だけ束縛していたため、階層ステートマシンでは子グラフのステートが
//   nullptr のまま残っていた。ランタイムは AnimControllerSystem の
//   BindGraphResources（再帰する）を別途通るので実害は出ていなかったが、
//   束縛経路が2つあって挙動が違うのは罠なので揃える。
void AnimGraphSerializer::BindSequencesRecursive(
    AnimStateGraph &graph, const std::vector<AnimSequence> &availableSequences)
{
    for (auto &state : graph.states)
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

        if (state.isSubMachine && state.subGraph)
            BindSequencesRecursive(*state.subGraph, availableSequences);
    }
}