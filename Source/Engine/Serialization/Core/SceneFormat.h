#pragma once
// シーン・プレハブのファイル形式の定数（マジックナンバー・バージョン・JSON キー）
// フィールドの追加だけならバージョンは据え置き。削除や型・意味の変更は +1 し、読込時の変換を書く
#include <cstdint>

namespace CCL::Serialization
{

    // マジックナンバー（'CCLS' を little-endian の uint32_t に詰めた値）。読込時に形式を判定する
    inline constexpr uint32_t SceneFileMagic = 0x53'4C'43'43u; // 'C','C','L','S'

    // 現在のファイル形式のバージョン
    inline constexpr uint32_t CurrentSceneVersion = 1;

    // 読み込める最も古いバージョン（これ未満は読込を拒否する）
    inline constexpr uint32_t MinSupportedSceneVersion = 1;

    // JSON のキー（綴り間違いを防ぐため定数にする）
    namespace Keys
    {
        inline constexpr const char *Magic       = "magic";
        inline constexpr const char *Version     = "version";
        inline constexpr const char *Timestamp   = "timestamp";
        inline constexpr const char *Entities    = "entities";
        inline constexpr const char *EntityLocal = "lid"; // ローカルインデックス
        inline constexpr const char *Components  = "components";
        inline constexpr const char *Name        = "name"; // プレハブ名
    }

}