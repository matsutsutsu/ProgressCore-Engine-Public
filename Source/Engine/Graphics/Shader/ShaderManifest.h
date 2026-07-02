/// @file  ShaderManifest.h
/// @brief シェーダー登録をデータ駆動化する仕組み。
///        Assets/Shader/shaders.json を読み、記載されたシェーダーを
///        ShaderRegistry へ一括登録する。マテリアルシェーダーは C++ を
///        一切書かずに JSON へ1行足すだけで追加できる（Graphics.cpp Phase3 の置換）。
#pragma once
#include <string>

class ShaderRegistry;

/**
 * @brief JSON マニフェストからシェーダーを一括登録する静的ユーティリティ。
 *
 * @details
 *  【設計意図】
 *   従来 Graphics.cpp Phase3 に手書きしていた RegisterMaterialShader /
 *   RegisterShader<T> の羅列を、外部 JSON (shaders.json) へ追い出す。
 *   - マテリアルシェーダー: JSON に1エントリ足すだけで増える（C++・再ビルド不要）。
 *   - カスタムシェーダー  : 独自 C++ クラスが要るものだけ、ShaderManifest.cpp の
 *                           レジストラ表にクラス名→RegisterShader<T> を1行追加する
 *                           （文字列から C++ 型は生成できないため、ここだけは避けられない）。
 *
 *  【JSON スキーマ】
 *  @code
 *   {
 *     "materialShaders": [
 *       { "name": "PBR" },                                    // 通常の不透明
 *       { "name": "GlowSigil", "blend": "additive" },         // 加算発光（深度書込なし/両面が既定に）
 *       { "name": "Grass",     "cull":  "none" },             // 両面描画
 *       { "name": "Marker",    "depth": "none" }              // 常に最前面
 *     ],
 *     "customShaders": [
 *       { "name": "Outline", "class": "OutlineShader" }
 *     ]
 *   }
 *  @endcode
 *
 *  【任意の PSO フィールド】(いずれも省略可。省略時はパス既定を使い、既存挙動を維持)
 *    "blend": opaque | transparency(alpha) | additive(add) | subtract | multiply
 *    "cull" : back | none
 *    "depth": testWrite(default) | testOnly(readOnly) | writeOnly | none | testLessEqual
 *    ※ blend が透明系のときはベースが自動で「深度書込なし・両面」になる。
 *       cull/depth を明示すればその軸だけ上書きできる。
 *
 *  【パス導出規約】(全シェーダー統一。name=N)
 *    cso : Assets/Shader/<N>VS.cso, Assets/Shader/<N>PS.cso
 *    hlsl: Shader/Material/<N>/<N>VS.hlsl, Shader/Material/<N>/<N>PS.hlsl
 *
 *  @note ファイル IO は必ず FileSystem 経由（将来の .pak 化/暗号化に透過対応するため）。
 *        本関数を呼ぶ前に FileSystem::Initialize() が完了していること。
 */
class ShaderManifest
{
public:
    /**
     * @brief マニフェストを読み込み、記載シェーダーを registry に登録する。
     * @param manifestVirtualPath VFS 仮想パス（例: "Assets/Shader/shaders.json"）。
     * @param registry 登録先。通常 ShaderRegistry::Instance()。
     * @return 1つ以上の登録に成功したら true。ファイル欠落/パース失敗/全滅で false。
     * @note 個々のエントリの失敗はスキップしてログに残し、可能な限り登録を続行する。
     */
    static bool LoadAndRegister(const std::string& manifestVirtualPath,
                               ShaderRegistry& registry);
};