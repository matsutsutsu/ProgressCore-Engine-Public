// =====================================================================
// SceneFormat.h
//   シーン/プレハブのファイルフォーマットに関する定数とヘッダ構造。
//
//   なぜ必要か:
//     ゲームを出荷した後でコンポーネントの仕様を変えたら、過去のセーブデータ・
//     ユーザー製作 MOD・既存シーンファイルが全て壊れる。
//     バージョン番号を入れておけば、ロード時にアップグレードロジックを噛ませて
//     旧フォーマットを救済できる。
//
//   Unreal の FCustomVersion / Unity の SerializationVersion と同じ思想。
//
//   バージョン採番ルール:
//     - 互換性を壊す変更を入れたら +1
//     - フィールド追加だけの変更は据え置き OK (デフォルト値で復元できるため)
//     - フィールド削除・型変更・意味変更は必ず +1 してアップグレード関数を書く
// =====================================================================
#pragma once
#include <cstdint>

namespace CCL::Serialization {

    // ファイルマジックナンバー ('CCLS' を little-endian で uint32_t に詰めた値)
    // ロード時にこの値を見て、フォーマットが正しいか即判定する。
    inline constexpr uint32_t SceneFileMagic = 0x53'4C'43'43u;   // 'C','C','L','S'

    // 現在のシーンフォーマットバージョン。
    // 互換性破壊を伴う変更を入れたら必ずインクリメントすること。
    inline constexpr uint32_t CurrentSceneVersion = 1;

    // サポートする最古バージョン (これ未満はロード拒否)
    inline constexpr uint32_t MinSupportedSceneVersion = 1;

    // ヘッダの JSON キー (タイポ防止のため定数化)
    namespace Keys {
        inline constexpr const char* Magic = "magic";
        inline constexpr const char* Version = "version";
        inline constexpr const char* EngineVer = "engineVersion";
        inline constexpr const char* Timestamp = "timestamp";
        inline constexpr const char* Entities = "entities";
        inline constexpr const char* EntityLocal = "lid";        // local index
        inline constexpr const char* Components = "components";
        inline constexpr const char* Name = "name";       // prefab name 等
    }

} // namespace CCL::Serialization