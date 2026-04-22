//#include "AnimGraphSerializer.h"
//#include "Engine/Platform/Logger.h" // プロジェクトのロガーに合わせてください
//#include <fstream>
//#include <cereal/archives/json.hpp>
//
//bool AnimGraphSerializer::SaveToJSON(const AnimStateGraph& graph, const std::string& filepath)
//{
//    std::ofstream os(filepath);
//    if (!os.is_open()) {
//        CCL_LOG_ERROR(LogCategory::Core, "Failed to open file for saving: %s", filepath.c_str());
//        return false;
//    }
//
//    try {
//        cereal::JSONOutputArchive archive(os);
//        archive(cereal::make_nvp("AnimStateGraph", graph));
//    }
//    catch (const std::exception& e) {
//        CCL_LOG_ERROR(LogCategory::Core, "Serialization failed: %s", e.what());
//        return false;
//    }
//
//    return true;
//}
//
//bool AnimGraphSerializer::LoadFromJSON(AnimStateGraph& outGraph, const std::string& filepath, const std::vector<AnimSequence>& availableSequences)
//{
//    std::ifstream is(filepath);
//    if (!is.is_open()) {
//        CCL_LOG_ERROR(LogCategory::Core, "Failed to open file for loading: %s", filepath.c_str());
//        return false;
//    }
//
//    try {
//        cereal::JSONInputArchive archive(is);
//        archive(cereal::make_nvp("AnimStateGraph", outGraph));
//
//        // =====================================================================
//        // ★ ポインタのバインド (Binding)
//        // JSONから読み込んだのは「文字列(sequenceName)」だけなので、
//        // メモリ上に存在する実際の AnimSequence を探してポインタを繋ぐ
//        // =====================================================================
//        for (auto& state : outGraph.states) {
//            state.sequence = nullptr; // 初期化
//
//            if (!state.sequenceName.empty()) {
//                // 利用可能なシーケンスリストから、名前が一致するものを探す
//                for (const auto& seq : availableSequences) {
//                    if (seq.sequenceName == state.sequenceName) {
//                        state.sequence = &seq;
//                        break;
//                    }
//                }
//
//                if (!state.sequence) {
//                    CCL_LOG_WARN(LogCategory::Core, "Animation Sequence not found: %s", state.sequenceName.c_str());
//                }
//            }
//        }
//    }
//    catch (const std::exception& e) {
//        CCL_LOG_ERROR(LogCategory::Core, "Deserialization failed: %s", e.what());
//        return false;
//    }
//
//    return true;
//}