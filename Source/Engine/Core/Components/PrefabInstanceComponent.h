#pragma once
#include <string>
#include <utility>

// =====================================================================
// PrefabInstanceComponent
//   「プレハブ実体(参照配置)」のルートに付与するマーカー。
//   prefabPath に元プレハブのパスを保持する。
//
//   役割:
//     ・ステージ(ベイク)保存では、このコンポーネントを持つエンティティの
//       subtree 全体が SceneSerializer から除外される(床/背景だけ焼く)。
//     ・レベル保存では、{prefabPath, transform} の「参照」として書き出される。
//       → プレハブ側を編集すれば全レベルに反映される。
//
//   ※ ComponentRegistry には登録しない(=シーンJSONには焼かれない)。
//      あくまでエディタ配置/レベル参照のためのランタイムマーカー。
// =====================================================================
struct PrefabInstanceComponent {
    std::string prefabPath;

    PrefabInstanceComponent() = default;
    explicit PrefabInstanceComponent(std::string path) : prefabPath(std::move(path)) {}
};
