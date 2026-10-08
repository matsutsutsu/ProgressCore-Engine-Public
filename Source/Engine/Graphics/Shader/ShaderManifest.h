#pragma once
// ===========================================================================
// 概要: Assets/Shader/shaders.json を読み、書かれたシェーダーを ShaderRegistry に登録する。
//
// 責務:
//   - materialShaders: 名前からファイルのパスを決め、MaterialShader として登録する
//   - customShaders: 専用の C++ クラスを持つシェーダーを、クラス名から登録する
//   - blend / cull / depth の指定から PSO の設定を作る
//
// 備考: マテリアルのシェーダーは JSON に1行足すだけで増やせる（C++ の変更もビルドも要らない）。
//       専用のクラスが要るものだけ、ShaderManifest.cpp の CustomRegistrars に
//       クラス名 → RegisterShader<T> の行を足す（文字列から C++ の型は作れないため）。
//
//       ファイルのパス（name = N）
//         cso : Assets/Shader/<N>VS.cso, Assets/Shader/<N>PS.cso
//         hlsl: Shader/Material/<N>/<N>VS.hlsl, Shader/Material/<N>/<N>PS.hlsl
//
//       PSO の指定（どれも省略できる。全部省略すると PSO を持たず、各パスの既定を使う）
//         "blend": opaque | transparency (alpha) | additive (add) | subtract | multiply
//         "cull" : back | none
//         "depth": testWrite (default) | testOnly (readOnly) | writeOnly | none | testLessEqual
//         blend が半透明の系統なら、深度を書かない・両面描画 を土台にする。
//         cull / depth を書けばその項目だけ変えられる。
// @code
//    {
//      "materialShaders": [
//        { "name": "PBR" },                            // 不透明
//        { "name": "GlowSigil", "blend": "additive" }, // 加算（深度を書かない・両面）
//        { "name": "Banner",    "cull":  "none" },     // 両面
//        { "name": "Marker",    "depth": "none" }      // 常に手前に描く
//      ],
//      "customShaders": [
//        { "name": "Outline", "class": "OutlineShader" }
//      ]
//    }
// @endcode
// ===========================================================================
#include <string>

class ShaderRegistry;

class ShaderManifest
{
public:
    /**
     * @brief shaders.json を読み、書かれたシェーダーを registry に登録する。
     * @param manifestVirtualPath FileSystem のパス（"Assets/Shader/shaders.json"）
     * @return 1つ以上登録できたら true。ファイルが無い・JSON が壊れている・1つも登録できないなら false
     * @note 失敗した項目はログに出して飛ばし、残りの登録を続ける。
     *       FileSystem::Initialize() の後に呼ぶ。
     */
    static bool LoadAndRegister(const std::string &manifestVirtualPath,
                                ShaderRegistry    &registry);
};