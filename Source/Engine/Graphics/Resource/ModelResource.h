#pragma once
// ===========================================================================
// 概要: 1つのモデルファイルの共有データ（ノード・メッシュ・マテリアル・アニメーション）
//
// 責務:
//   - glTF（.glb / .gltf）かモデル定義ファイル（.model.json）を読み込む
//   - 読んだ結果を .cereal にキャッシュし、次回からはそれを読む
//   - 頂点・インデックスバッファとテクスチャなど、GPU のリソースを作る
//   - メッシュごとと、モデル全体のローカル AABB を求める
//
// 備考: ResourceManager が1ファイルにつき1つだけ持ち、同じモデルの全インスタンス（Model）が共有する
//       .model.json の書き方は ModelResource.cpp の LoadModelSet の前のコメントを参照
// ===========================================================================
#include <string>
#include <vector>
#include <wrl.h>
#include <d3d11.h>
#include <map>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Engine/Graphics/Resource/GraphicsTypes.h"

class ModelResource
{
public:
    ModelResource() {}
    virtual ~ModelResource() {}

    static const std::vector<D3D11_INPUT_ELEMENT_DESC> InputElementDescs; // Vertex に合わせた入力レイアウト

    using NodeId = UINT64;

    struct Node
    {
        // id と path は今は使っていないが、.cereal キャッシュの形式に含まれるので残している
        NodeId      id;
        std::string name;             // ノード名（例: "Head"）。アニメーションの対応づけにも使う
        std::string path;
        int         parentIndex = -1; // 親ノードの番号（-1 ならルート）

        // 初期姿勢（親ノードに対する位置・回転・スケール）
        DirectX::XMFLOAT3 scale     = {1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT4 rotate    = {0.0f, 0.0f, 0.0f, 1.0f}; // クォータニオンの単位元
        DirectX::XMFLOAT3 translate = {0.0f, 0.0f, 0.0f};

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    // スキニング用のボーン情報
    struct Bone
    {
        int                 nodeIndex;       // 対応するノードの番号
        DirectX::XMFLOAT4X4 offsetTransform; // モデル空間からボーン空間への変換行列（逆バインド行列）

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    // テクスチャ・カラーなどのマテリアル情報
    struct Material
    {
        std::string name; // マテリアル名

        // テクスチャのファイル名（モデルのディレクトリからの相対パス）
        std::string baseTextureFileName;
        std::string normalTextureFileName;
        std::string emissiveTextureFileName;
        std::string occlusionTextureFileName;
        std::string metalnessRoughnessTextureFileName;

        // 値
        DirectX::XMFLOAT4 baseColor         = {1, 1, 1, 1};
        DirectX::XMFLOAT3 emissiveColor     = {1, 1, 1};
        float             metalness         = 0.0f;
        float             roughness         = 0.0f;
        float             occlusionStrength = 0.0f;
        float             alphaCutoff       = 0.5f;
        AlphaMode         alphaMode         = AlphaMode::Opaque;

        // 読み込んだテクスチャ（BuildModel が作る。保存しない）
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> baseMap;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normalMap;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> emissiveMap;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> occlusionMap;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> metalnessRoughnessMap;

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    // 1つのメッシュの中で、同じマテリアルで描く範囲
    struct Subset
    {
        UINT startIndex    = 0; // 描き始めるインデックスの位置
        UINT indexCount    = 0; // インデックスの数
        int  materialIndex = 0; // 使うマテリアルの番号

        Material *material = nullptr; // materials の要素（所有しない。BuildModel が入れる）

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    // LOD の1段分。Mesh::lodIndexBuffer の中で、この段のインデックスが入っている範囲
    struct LodLevel
    {
        UINT startIndex = 0; // lodIndexBuffer の中の開始位置
        UINT indexCount = 0; // インデックスの数
    };

    struct Vertex
    {
        DirectX::XMFLOAT3 position = {0, 0, 0};
        DirectX::XMFLOAT3 normal   = {0, 0, 0};
        DirectX::XMFLOAT4 tangent  = {0, 0, 0, 0}; // 接線。w は従接線の向き（±1）
        DirectX::XMFLOAT2 texcoord = {0, 0};

        // スキニング（最大4本のボーン）
        DirectX::XMFLOAT4 boneWeight = {1, 0, 0, 0}; // 各ボーンの重み
        DirectX::XMUINT4  boneIndex  = {0, 0, 0, 0}; // Mesh::bones の中の番号

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    struct Mesh
    {
        std::vector<Vertex> vertices;
        std::vector<UINT>   indices;
        std::vector<Subset> subsets; // マテリアルごとの描画範囲
        std::vector<Bone>   bones;   // このメッシュを動かすボーン。空ならスキンメッシュではない

        int nodeIndex;     // このメッシュが付いているノードの番号
        int materialIndex; // メッシュ全体のマテリアルの番号

        // nodeIndices と offsetTransforms は今は読んでいないが、.cereal キャッシュの形式に含まれるので残している
        std::vector<int>                 nodeIndices;
        std::vector<DirectX::XMFLOAT4X4> offsetTransforms;

        // ローカル空間の AABB（ComputeMeshBounds が求める。カリングに使う）
        DirectX::XMFLOAT3 boundsMin;
        DirectX::XMFLOAT3 boundsMax;

        // GPU のバッファ（BuildModel が作る。保存しない）
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;

        // LOD（GenerateLods が作る。保存しない）。lods が空なら LOD なしで、indexBuffer をそのまま描く
        // lodIndexBuffer には LOD0（indices の写し）から順に全段を詰めてあり、頂点バッファは全段で共有する
        std::vector<LodLevel>                lods;           // [0] が LOD0（元の形）。後ろほど粗い
        Microsoft::WRL::ComPtr<ID3D11Buffer> lodIndexBuffer; // 全段のインデックス

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    // 位置・スケールのキーフレーム
    struct VectorKeyframe
    {
        float             seconds; // 時刻（秒）
        DirectX::XMFLOAT3 value;

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    // 回転（クォータニオン）のキーフレーム
    struct QuaternionKeyframe
    {
        float             seconds; // 時刻（秒）
        DirectX::XMFLOAT4 value;

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    // 1ノード分のキーフレーム
    struct NodeAnim
    {
        std::vector<VectorKeyframe>     positionKeyframes;
        std::vector<QuaternionKeyframe> rotationKeyframes;
        std::vector<VectorKeyframe>     scaleKeyframes;

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    struct Animation
    {
        std::string           name;
        float                 secondsLength; // 長さ（秒）
        std::vector<NodeAnim> nodeAnims;     // nodes と同じ並び（添字 = ノードの番号）

        template <class Archive>
        void serialize(Archive &archive, int version);
    };

    const std::vector<Mesh>      &GetMeshes() const { return meshes; }
    const std::vector<Node>      &GetNodes() const { return nodes; }
    const std::vector<Animation> &GetAnimations() const { return animations; }
    // エディタでアニメーションを書き換えるとき用
    std::vector<Animation>      &GetAnimationsMutable() { return animations; }
    const std::vector<Material> &GetMaterials() const { return materials; }


    // .glb / .gltf / .model.json を読む（.cereal があればそれを読む）。GPU のリソースも作る
    void Load(ID3D11Device *device, const char *filename);

    // 別ファイルのアニメーションを読み、animations の末尾に足す（ボーン名で対応づける）
    void AppendAnimations(const char *filename);

    // 位置のリターゲットをするか（.model.json の "translationRetargeting"）
    //   true（既定）なら、ルート・Pelvis・Hips 以外はバインドポーズの骨の長さを使う。false ならアニメーションの移動をそのまま使う
    //   cereal には保存せず、キャッシュから読んだときも毎回 JSON の値で上書きする
    bool  GetTranslationRetargeting() const { return translationRetargeting; }
    void  SetTranslationRetargeting(bool enable) { translationRetargeting = enable; }

    // モデル定義ファイル（.model.json。本体と追加のアニメーションを1つの JSON で指定する）か
    static bool IsModelSetFile(const char *filename);

    // 頂点から mesh.boundsMin / boundsMax を求める
    void ComputeMeshBounds(Mesh &mesh);

    // 全メッシュの AABB を合わせて、モデル全体の AABB を求める。スキンでないメッシュは、ノードの初期姿勢の位置で合わせる
    void ComputeModelLocalAABB();

    // モデル全体のローカル AABB
    const DirectX::BoundingBox &GetBoundingBox() const { return m_boundingBox; }

    // 全メッシュの LOD を、meshoptimizer で元の形を簡略化して作る
    // 描画に使われ始めてから呼ぶと描画中のバッファを書き換えるので、ResourceManager が読み込んだ直後にだけ呼ぶ
    // スキンメッシュと、マテリアルの範囲（subsets）が2つ以上あるメッシュには作らない
    void GenerateLods(ID3D11Device *device);

    // GenerateLods にかかった時間（ms）。作っていなければ 0
    float GetLodBuildMilliseconds() const { return m_lodBuildMs; }

protected:
    // .model.json を読む。Load() から呼ばれ、キャッシュの判定・GPU リソースの作成・AABB まで行う
    void LoadModelSet(ID3D11Device *device, const char *filename);

    // GPU のリソース（テクスチャ・頂点・インデックスバッファ）を作る。dirname はテクスチャを探す起点
    void BuildModel(ID3D11Device *device, const char *dirname);

    // .cereal へ書き出す・.cereal から読む
    void Serialize(const char *filename);
    void Deserialize(const char *filename);

    // 1つのメッシュの LOD を作り、mesh.lods と mesh.lodIndexBuffer に入れる（GenerateLods から呼ぶ）
    void BuildMeshLods(ID3D11Device *device, Mesh &mesh);

protected:
    std::vector<Node>      nodes;
    std::vector<Material>  materials;
    std::vector<Mesh>      meshes;
    std::vector<Animation> animations;

    bool translationRetargeting = true;

    DirectX::BoundingBox m_boundingBox; // モデル全体のローカルAABB

    float m_lodBuildMs = 0.0f; // GenerateLods にかかった時間（ms）
};
