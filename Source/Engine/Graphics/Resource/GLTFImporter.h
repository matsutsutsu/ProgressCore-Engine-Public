#pragma once
// ===========================================================================
// 概要: tinygltf で glTF（.glb / .gltf）を読み、ModelResource の形に写すクラス
//
// 責務:
//   - ノード・メッシュ・マテリアル・アニメーションを読む
//   - 埋め込みテクスチャをファイルに書き出す（テクスチャの読み込みは ModelResource::BuildModel）
//   - 右手系（glTF）から左手系（D3D）へ変換し、接線がなければ求める
//
// 備考: CPU のメモリに読むだけで、GPU のリソースは作らない
// ===========================================================================
#include <map>
#include <filesystem>
#include <tiny_gltf.h>
#include "ModelResource.h"

class GLTFImporter
{
private:
    using MeshList      = std::vector<ModelResource::Mesh>;
    using MaterialList  = std::vector<ModelResource::Material>;
    using NodeList      = std::vector<ModelResource::Node>;
    using AnimationList = std::vector<ModelResource::Animation>;

public:
    GLTFImporter(const char *filename);

    // ノードを glTF の並び順のまま読む（並べ替えはしない。親が先になる計算の順番は Model が作る）
    void LoadNodes(NodeList &nodes);

    // メッシュを読む（primitive 1つ = メッシュ1つ）
    void LoadMeshes(MeshList &meshes, const NodeList &nodes);

    // マテリアルを読み、埋め込みテクスチャはファイルに書き出す
    void LoadMaterials(MaterialList &materials, ID3D11Device *device = nullptr);

    // アニメーションを読む。適用先のボーンは名前で nodes の番号に読み替える
    void LoadAnimations(AnimationList &animations, const NodeList &nodes, float sampleRate = 60);

private:
    // glTF の double の配列から DirectXMath の型へ
    static DirectX::XMFLOAT3 gltfVector3ToXMFLOAT3(const std::vector<double> &gltfValue);

    static DirectX::XMFLOAT4 gltfQuaternionToXMFLOAT4(const std::vector<double> &gltfValue);

    static DirectX::XMFLOAT4X4 gltfMatrixToXMFLOAT4X4(const std::vector<double> &gltfValue);

    // 右手系（glTF）から左手系（D3D）への変換
    static void ConvertPositionAxisSystem(DirectX::XMFLOAT3 &v);
    static void ConvertPositionAxisSystem(DirectX::XMFLOAT4 &v);
    static void ConvertRotationAxisSystem(DirectX::XMFLOAT4 &q);
    static void ConvertMatrixAxisSystem(DirectX::XMFLOAT4X4 &m);
    static void ConvertNodeAxisSystem(ModelResource::Node &node);
    static void ConvertMeshAxisSystem(ModelResource::Mesh &mesh);
    static void ConvertAnimationAxisSystem(ModelResource::Animation &animation);

    // 頂点の位置と UV から接線を求める（接線が入っていない glTF 用）
    static void ComputeTangents(std::vector<ModelResource::Vertex> &vertices, const std::vector<uint32_t> &indices);

private:
    std::filesystem::path filepath;  // 読み込み元（埋め込みテクスチャの書き出し先の基準にもなる）
    tinygltf::Model       gltfModel; // tinygltf が読んだ中身
};
