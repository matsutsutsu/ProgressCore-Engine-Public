#include <filesystem>
#include <fstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "GLTFImporter.h"
#include "Model.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include <array>
#include <algorithm> // std::lower_bound用
#include <cctype>
#include <tracy/Tracy.hpp>

// ヘルパー関数：大文字小文字を無視した部分一致。
//   ロード時に「Root/Pelvis/Hips 系ボーンか」を一度だけ判定するために使う。
//   ※ 旧実装はこの判定を ComputeAnimation 内で骨ごと・毎フレーム実行しており、
//     std::string のヒープ確保が積み重なって処理落ちの主要因になっていた。
static bool ContainsCI(const std::string &name, const char *needle)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(needle) != std::string::npos;
}

// ヘルパー関数：キーフレームの二分探索と補間率の計算
template <typename T>
void EvaluateKeyframes(const std::vector<T> &keyframes, float time, float &outRate, const T *&outK0, const T *&outK1)
{
    if (keyframes.empty())
    {
        outK0 = outK1 = nullptr;
        return;
    }
    // キーフレームが1つ、または時間が最初のキーより前の場合（クランプ）
    if (keyframes.size() == 1 || time <= keyframes.front().seconds)
    {
        outK0 = outK1 = &keyframes.front();
        outRate       = 0.0f;
        return;
    }
    // 時間が最後のキーより後の場合（クランプ）
    if (time >= keyframes.back().seconds)
    {
        outK0 = outK1 = &keyframes.back();
        outRate       = 0.0f;
        return;
    }

    // 二分探索 (O(log N)) で現在の時間を超える最初のキーフレームを探す
    auto it = std::lower_bound(keyframes.begin(), keyframes.end(), time,
        [](const T &kf, float t) { return kf.seconds < t; });

    outK1 = &(*it);
    outK0 = &(*(it - 1));

    // ★ ゼロ除算ガード: UE5等の書き出しでは、IK切り替えや瞬間ポーズ変更を表現するために
    //   同一時刻(またはごく僅かな差)に2つのキーフレームが置かれることがある。
    //   分母がほぼ0のまま除算すると outRate が NaN/Inf になり、その姿勢がそのまま
    //   Slerp/Lerpに渡って該当ボーンの向きが破綻する（"特定の部位だけおかしい"の一因になりうる）。
    //   実質的にステップ変化とみなし、後側のキーフレームの値をそのまま採用する。
    const float delta = outK1->seconds - outK0->seconds;
    if (delta <= 1e-6f)
    {
        outRate = 0.0f;
        outK0   = outK1; // 補間せず後側の値をそのまま使う
    }
    else
    {
        outRate = (time - outK0->seconds) / delta;
    }
}

// コンストラクタ
Model::Model(ID3D11Device *device, const char *filename, float sampleRate)
    : filePath(filename)
{
    // ファイル名が空の場合（動的生成モード）
    // ファイル読み込みを行わず、空のリソースだけ作って終了する
    if (filePath.empty())
    {
        // LoadModelResourceではなく、AllocateEmptyModelResourceを呼ぶ。
        // これで、Load()でクラッシュすることも、他の動的モデルとメモリが混ざることも絶対に無くなります。
        resource = ResourceManager::Instance().AllocateEmptyModelResource();
        return;
    }

    resource = ResourceManager::Instance().LoadModelResource(filename);
    if (!resource.IsValid())
    {
        _ASSERT_EXPR(false, L"ModelResource が取得できませんでした。");
        return;
    }

    // ModelResourceに格納された階層情報（Nodes）を
    // Modelのインスタンスにコピーして親子関係を再構築する
    const std::vector<ModelResource::Node> &resNodes = ResourceManager::Instance().GetModel(resource)->GetNodes();
    nodes.resize(resNodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const ModelResource::Node &src = resNodes.at(i);
        Node                      &dst = nodes.at(i);

        if (src.parentIndex >= 0)
        {
            dst.parent = &nodes.at(src.parentIndex);
        }

        dst.name     = src.name;
        dst.position = src.translate;
        dst.rotation = src.rotate;
        dst.scale    = src.scale;

        // ★ 位置リターゲット対象フラグをここで一度だけ確定（毎フレームの文字列判定を廃止）。
        //   index 0（ルート）と Pelvis/Hips/Root 系ボーンだけがアニメの位置移動をそのまま使う。
        dst.isRootOrPelvis = (i == 0) ||
                             ContainsCI(src.name, "pelvis") ||
                             ContainsCI(src.name, "hips") ||
                             ContainsCI(src.name, "root");

        dst.children.clear();

        // 行列初期化（単位行列）
        XMStoreFloat4x4(&dst.localTransform, DirectX::XMMatrixIdentity());
        XMStoreFloat4x4(&dst.globalTransform, DirectX::XMMatrixIdentity());
        XMStoreFloat4x4(&dst.worldTransform, DirectX::XMMatrixIdentity());
    }
    // ② 親子関係の再構築
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto &src = resNodes[i];
        if (src.parentIndex >= 0 && src.parentIndex < (int)nodes.size())
        {
            nodes[i].parent = &nodes[src.parentIndex];
            nodes[src.parentIndex].children.push_back(&nodes[i]);
        }
    }

    // リソース側のマテリアルを参照
    const std::vector<ModelResource::Material> &resMaterials = ResourceManager::Instance().GetModel(resource)->GetMaterials();
    // モデル側マテリアルを用意
    materials.resize(resMaterials.size());

    for (size_t i = 0; i < resMaterials.size(); ++i)
    {
        const auto &src = resMaterials[i];
        auto       &dst = materials[i];

        // ---- 参照を保存 ----
        dst.name = src.name;

        // 純粋なデータの実体を作成する
        dst.data = std::make_shared<MaterialData>();

        // --- データを辞書へマッピング ---
        // デフォルトシェーダーID（必要に応じて変更）
        // アルファモードに応じてシェーダーを切り替える例
        /*
        if (dst.data->alphaMode == AlphaMode::Opaque)
            dst.data->shaderId = ShaderId::Phong; // または Toon
        else
            dst.data->shaderId = ShaderId::Toon; // 半透明対応など
        */
        dst.data->shaderHash = "PBR"_hash; // デフォルト

        // アルファモードのコピー（型変換）
        dst.data->alphaMode   = static_cast<AlphaMode>(src.alphaMode);
        dst.data->alphaCutoff = src.alphaCutoff;

        // 1. カラー情報
        dst.data->SetColor("materialColor"_hash, src.baseColor); // 従来のPhong/Basicシェーダー用
        dst.data->SetColor("baseColor"_hash, src.baseColor);     // 新しいPBRシェーダー用
        // emissiveColor (float3) を float4 に変換して格納
        dst.data->SetColor("emissiveColor"_hash, {src.emissiveColor.x, src.emissiveColor.y, src.emissiveColor.z, 1.0f});

        // 2. スカラー情報
        dst.data->SetScalar("metalness"_hash, src.metalness);
        dst.data->SetScalar("roughness"_hash, src.roughness);
        dst.data->SetScalar("occlusionStrength"_hash, src.occlusionStrength);
        dst.data->SetScalar("alphaCutoff"_hash, src.alphaCutoff);

        // 3. ToonShader用のデフォルト値（これがないと黒くなる）
        dst.data->SetColor("rimColor"_hash, {1.0f, 1.0f, 1.0f, 1.0f});
        dst.data->SetScalar("rimPower"_hash, 5.0f);
        dst.data->SetScalar("rimIntensity"_hash, 1.0f);

        // トゥーン用デフォルトランプを登録
        dst.data->SetTexture("RampTexture"_hash, ResourceManager::Instance().LoadTexture("Assets/Textures/Shader/ramp.png"));

        // OutlineShader用のデフォルト値
        dst.data->SetColor("color"_hash, {0.0f, 0.0f, 0.0f, 1.0f}); // 黒い線
        dst.data->SetScalar("size"_hash, 0.02f);                    // 太さ

        // 4. テクスチャ情報
        if (src.baseMap)
        {
            auto handle = ResourceManager::Instance().RegisterTexture(src.baseMap);
            dst.data->SetTexture("DiffuseMap"_hash, handle);
            dst.data->SetTexture("AlbedoMap"_hash, handle);
            dst.data->SetScalar("useAlbedoMap"_hash, 1.0f); // ★フラグON
        }
        else
        {
            dst.data->SetScalar("useAlbedoMap"_hash, 0.0f); // ★フラグOFF
        }

        if (src.normalMap)
        {
            dst.data->SetTexture("NormalMap"_hash, ResourceManager::Instance().RegisterTexture(src.normalMap));
            dst.data->SetScalar("useNormalMap"_hash, 1.0f);
        }
        else
        {
            dst.data->SetScalar("useNormalMap"_hash, 0.0f);
        }

        if (src.emissiveMap)
        {
            dst.data->SetTexture("EmissiveMap"_hash, ResourceManager::Instance().RegisterTexture(src.emissiveMap));
        }

        if (src.occlusionMap)
        {
            dst.data->SetTexture("OcclusionMap"_hash, ResourceManager::Instance().RegisterTexture(src.occlusionMap));
        }

        if (src.metalnessRoughnessMap)
        {
            auto handle = ResourceManager::Instance().RegisterTexture(src.metalnessRoughnessMap);
            dst.data->SetTexture("MetalnessRoughnessMap"_hash, handle);
            dst.data->SetTexture("MetalRoughMap"_hash, handle);
            dst.data->SetScalar("useMetalRoughMap"_hash, 1.0f); // ★フラグON
        }
        else
        {
            dst.data->SetScalar("useMetalRoughMap"_hash, 0.0f); // ★フラグOFF
        }
    }

    const std::vector<ModelResource::Mesh> &resMeshes = ResourceManager::Instance().GetModel(resource)->GetMeshes();
    meshes.resize(resMeshes.size());
    for (size_t i = 0; i < resMeshes.size(); ++i)
    {
        const auto &src = resMeshes[i];
        auto       &dst = meshes[i];

        // ---- 参照を保存 ----
        dst.data     = &src;
        dst.material = &materials.at(src.materialIndex); // ここでComponentを持つMaterialへの参照が入る

        // ModelResource::Mesh はそのメッシュが属する nodeIndex を持っている [cite: 15]
        if (src.nodeIndex >= 0 && src.nodeIndex < (int)nodes.size())
        {
            dst.node = &nodes.at(src.nodeIndex);
        }
        else
        {
            dst.node = nullptr;
        }

        dst.bones.reserve(src.bones.size());
        for (const ModelResource::Bone &srcBone : src.bones)
        {
            Model::Bone &dstBone = dst.bones.emplace_back();
            dstBone.data         = &srcBone;
            dstBone.node         = &nodes.at(srcBone.nodeIndex);
        }
    }

    // 行列初期化
    DirectX::XMFLOAT4X4 worldTransform;
    DirectX::XMStoreFloat4x4(&worldTransform, DirectX::XMMatrixIdentity());
    UpdateTransform(worldTransform);
}

// アニメーションインデックス取得（名前→indexをハッシュ表でO(1)取得）
int Model::GetAnimationIndex(const char *name) const
{
    if (!resource.IsValid())
    {
        return -1;
    } // IsValidに変更

    // 初回だけ名前→indexの表を構築する（ロード後は不変なので以降は再利用）。
    if (!_animIndexBuilt)
    {
        const auto &animations = ResourceManager::Instance().GetModel(resource)->GetAnimations();
        _animIndexCache.clear();
        _animIndexCache.reserve(animations.size());
        for (size_t i = 0; i < animations.size(); ++i)
        {
            // 名前衝突時は先勝ち（従来の線形探索と同じ「最初に一致したもの」を維持）。
            _animIndexCache.emplace(CCL::Utils::HashString(animations[i].name), static_cast<int>(i));
        }
        _animIndexBuilt = true;
    }

    auto it = _animIndexCache.find(CCL::Utils::HashString(name));
    return (it != _animIndexCache.end()) ? it->second : -1;
}

// ノードインデックス取得
int Model::GetNodeIndex(const char *name) const
{
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
    {
        if (nodes.at(nodeIndex).name == name)
        {
            return static_cast<int>(nodeIndex);
        }
    }
    return -1;
}

// トランスフォーム更新処理
void Model::UpdateTransform(const DirectX::XMFLOAT4X4 &worldTransform)
{
    DirectX::XMMATRIX ParentWorldTransform = DirectX::XMLoadFloat4x4(&worldTransform);

    for (Node &node : nodes)
    {
        // ローカル行列算出
        DirectX::XMMATRIX S              = DirectX::XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z);
        DirectX::XMMATRIX R              = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.rotation));
        DirectX::XMMATRIX T              = DirectX::XMMatrixTranslation(node.position.x, node.position.y, node.position.z);
        DirectX::XMMATRIX LocalTransform = S * R * T;

        // グローバル行列算出
        DirectX::XMMATRIX ParentGlobalTransform;
        if (node.parent != nullptr)
        {
            ParentGlobalTransform = DirectX::XMLoadFloat4x4(&node.parent->globalTransform);
        }
        else
        {
            ParentGlobalTransform = DirectX::XMMatrixIdentity();
        }
        DirectX::XMMATRIX GlobalTransform = LocalTransform * ParentGlobalTransform;

        // ワールド行列算出
        DirectX::XMMATRIX WorldTransform = GlobalTransform * ParentWorldTransform;

        // 計算結果を格納
        DirectX::XMStoreFloat4x4(&node.localTransform, LocalTransform);
        DirectX::XMStoreFloat4x4(&node.globalTransform, GlobalTransform);
        DirectX::XMStoreFloat4x4(&node.worldTransform, WorldTransform);
    }
}

// 単一ノードのポーズ評価の実体（取得済みの Animation を受け取る内部ヘルパー）。
//   ResourceManager 取得を含めないことで、vector 版がノード数ぶん引き直す無駄を無くす。
void Model::ComputeNodePose(const ModelResource::Animation &animation, int nodeIndex, float time, NodePose &nodePose) const
{
    ZoneScopedN("Model::ComputeNodePose");

    const ModelResource::NodeAnim &nodeAnim = animation.nodeAnims.at(nodeIndex);

    // デフォルト値として、モデルの本来のバインドポーズ（初期姿勢）を設定しておく
    // これによりアニメーションデータが存在しないチャンネルが0リセットされるのを防ぐ
    const Node &bindNode = nodes.at(nodeIndex);
    nodePose.position    = bindNode.position;
    nodePose.rotation    = bindNode.rotation;
    nodePose.scale       = bindNode.scale;

    float rate;

    // --- 位置 (Translation) の評価 ---
    const ModelResource::VectorKeyframe *posK0, *posK1;
    EvaluateKeyframes(nodeAnim.positionKeyframes, time, rate, posK0, posK1);
    if (posK0 && posK1)
    {
        DirectX::XMVECTOR V0 = DirectX::XMLoadFloat3(&posK0->value);
        DirectX::XMVECTOR V1 = DirectX::XMLoadFloat3(&posK1->value);
        DirectX::XMVECTOR V  = DirectX::XMVectorLerp(V0, V1, rate);
        DirectX::XMStoreFloat3(&nodePose.position, V);
    }

    // ★ Translation Retargeting（プロポーション崩壊対策）。
    //   Root/Pelvis/Hips 系以外は、アニメの位置移動を無視してモデル本来の骨の長さ
    //   (bindNode.position)で上書きする。対象かどうかはロード時に確定した isRootOrPelvis を
    //   見るだけ（旧実装はここで骨名を毎フレーム小文字化＋部分一致しており処理落ちの主因だった）。
    if (!bindNode.isRootOrPelvis)
    {
        nodePose.position = bindNode.position;
    }

    // --- 回転 (Rotation) の評価 ---
    const ModelResource::QuaternionKeyframe *rotK0, *rotK1;
    EvaluateKeyframes(nodeAnim.rotationKeyframes, time, rate, rotK0, rotK1);
    if (rotK0 && rotK1)
    {
        DirectX::XMVECTOR Q0 = DirectX::XMLoadFloat4(&rotK0->value);
        DirectX::XMVECTOR Q1 = DirectX::XMLoadFloat4(&rotK1->value);
        DirectX::XMVECTOR Q  = DirectX::XMQuaternionSlerp(Q0, Q1, rate);
        DirectX::XMStoreFloat4(&nodePose.rotation, Q);
    }

    // --- スケール (Scale) の評価 ---
    const ModelResource::VectorKeyframe *sclK0, *sclK1;
    EvaluateKeyframes(nodeAnim.scaleKeyframes, time, rate, sclK0, sclK1);
    if (sclK0 && sclK1)
    {
        DirectX::XMVECTOR V0 = DirectX::XMLoadFloat3(&sclK0->value);
        DirectX::XMVECTOR V1 = DirectX::XMLoadFloat3(&sclK1->value);
        DirectX::XMVECTOR V  = DirectX::XMVectorLerp(V0, V1, rate);
        DirectX::XMStoreFloat3(&nodePose.scale, V);
    }
}

void Model::ComputeAnimation(int animationIndex, int nodeIndex, float time, NodePose &nodePose) const
{
    ZoneScopedN("Model::ComputeAnimation");

    if (!resource.IsValid()) return;

    const auto &animations = ResourceManager::Instance().GetModel(resource)->GetAnimations();
    ComputeNodePose(animations.at(animationIndex), nodeIndex, time, nodePose);
}

// アニメーション計算（全ノード）
void Model::ComputeAnimation(int animationIndex, float time, std::vector<NodePose> &nodePoses) const
{
    ZoneScopedN("Model::ComputeAnimation");

    if (!resource.IsValid()) return;

    if (nodePoses.size() != nodes.size())
    {
        nodePoses.resize(nodes.size());
    }

    // ★ ResourceManager 取得と Animation 参照はループ外で1回だけ（旧実装はノード数ぶん引き直していた）。
    const auto                     &animations = ResourceManager::Instance().GetModel(resource)->GetAnimations();
    const ModelResource::Animation &animation  = animations.at(animationIndex);

    for (size_t nodeIndex = 0; nodeIndex < nodePoses.size(); ++nodeIndex)
    {
        ComputeNodePose(animation, static_cast<int>(nodeIndex), time, nodePoses.at(nodeIndex));
    }
}

// ノードポーズ設定
void Model::SetNodePoses(const std::vector<NodePose> &nodePoses)
{
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
    {
        const NodePose &pose = nodePoses.at(nodeIndex);
        Node           &node = nodes.at(nodeIndex);

        node.position = pose.position;
        node.rotation = pose.rotation;
        node.scale    = pose.scale;
    }
}

// ノードポーズ取得
void Model::GetNodePoses(std::vector<NodePose> &nodePoses) const
{
    if (nodePoses.size() != nodes.size())
    {
        nodePoses.resize(nodes.size());
    }
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
    {
        const Node &node = nodes.at(nodeIndex);
        NodePose   &pose = nodePoses.at(nodeIndex);

        pose.position = node.position;
        pose.rotation = node.rotation;
        pose.scale    = node.scale;
    }
}

// アニメーション補間処理
void Model::BlendAnimations(
    const std::vector<NodePose> &animation0,
    const std::vector<NodePose> &animation1,
    float                        blendRate,
    std::vector<NodePose>       &result)
{
    size_t nodeCount = animation0.size();
    result.resize(nodeCount);

    for (size_t i = 0; i < nodeCount; ++i)
    {
        const Model::NodePose &pose0       = animation0[i];
        const Model::NodePose &pose1       = animation1[i];
        Model::NodePose       &blendedPose = result[i];

        // 位置の線形補間
        DirectX::XMVECTOR pos0       = DirectX::XMLoadFloat3(&pose0.position);
        DirectX::XMVECTOR pos1       = DirectX::XMLoadFloat3(&pose1.position);
        DirectX::XMVECTOR blendedPos = DirectX::XMVectorLerp(pos0, pos1, blendRate);
        DirectX::XMStoreFloat3(&blendedPose.position, blendedPos);

        // 回転のスフィア線形補間
        DirectX::XMVECTOR rot0       = DirectX::XMLoadFloat4(&pose0.rotation);
        DirectX::XMVECTOR rot1       = DirectX::XMLoadFloat4(&pose1.rotation);
        DirectX::XMVECTOR blendedRot = DirectX::XMQuaternionSlerp(rot0, rot1, blendRate);
        DirectX::XMStoreFloat4(&blendedPose.rotation, blendedRot);

        // スケールの線形補間
        DirectX::XMVECTOR scale0       = DirectX::XMLoadFloat3(&pose0.scale);
        DirectX::XMVECTOR scale1       = DirectX::XMLoadFloat3(&pose1.scale);
        DirectX::XMVECTOR blendedScale = DirectX::XMVectorLerp(scale0, scale1, blendRate);
        DirectX::XMStoreFloat3(&blendedPose.scale, blendedScale);
    }
}

const ModelResource *Model::GetResource() const
{
    return ResourceManager::Instance().GetModel(resource);
}

ModelResource *Model::GetResourceMutable()
{
    return ResourceManager::Instance().GetModel(resource);
}

std::shared_ptr<Model> Model::CreateFromData(ID3D11Device *device,
    const std::vector<ModelResource::Vertex>              &vertices,
    const std::vector<uint32_t>                           &indices,
    std::shared_ptr<MaterialData>                          baseMaterial)
{
    // 1. 空のモデルインスタンスを作成
    // ダミーパスを渡すが、ロード失敗前提で進める
    auto model = std::make_shared<Model>(device, "");

    // リソースを新規作成して上書き
    auto *res = ResourceManager::Instance().GetModel(model->resource);

    // =================================================
    // 2. リソースデータの構築 (ModelResource)
    // =================================================

    // --- Meshの構築 ---
    ModelResource::Mesh mesh;
    mesh.vertices = vertices;
    mesh.indices  = indices;

    // GPUバッファ作成
    // (GpuResourceUtils に追加した関数を使用)
    if (!mesh.vertices.empty())
    {
        GpuResourceUtils::CreateVertexBuffer(device,
            mesh.vertices.data(),
            static_cast<UINT>(mesh.vertices.size() * sizeof(ModelResource::Vertex)),
            mesh.vertexBuffer.GetAddressOf());
    }
    if (!mesh.indices.empty())
    {
        GpuResourceUtils::CreateIndexBuffer(device,
            mesh.indices.data(),
            static_cast<UINT>(mesh.indices.size() * sizeof(uint32_t)),
            mesh.indexBuffer.GetAddressOf());
    }

    // メンバ変数の設定
    mesh.nodeIndex     = 0; // ルートノードに紐付け
    mesh.materialIndex = 0; // 0番目のマテリアルを使用

    // Subsetの設定 (ここが重要：構造体定義に合わせる)
    ModelResource::Subset subset;
    subset.startIndex    = 0; // indexStart ではなく startIndex
    subset.indexCount    = static_cast<UINT>(mesh.indices.size());
    subset.materialIndex = 0;
    subset.material      = nullptr; // リソース内ではポインタ解決前なのでnull
    mesh.subsets.push_back(subset);

    // バウンディングボックス計算
    mesh.boundsMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    mesh.boundsMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    if (mesh.vertices.empty())
    {
        mesh.boundsMin = {0, 0, 0};
        mesh.boundsMax = {0, 0, 0};
    }
    else
    {
        for (const auto &v : mesh.vertices)
        {
            mesh.boundsMin.x = (std::min)(mesh.boundsMin.x, v.position.x);
            mesh.boundsMin.y = (std::min)(mesh.boundsMin.y, v.position.y);
            mesh.boundsMin.z = (std::min)(mesh.boundsMin.z, v.position.z);
            mesh.boundsMax.x = (std::max)(mesh.boundsMax.x, v.position.x);
            mesh.boundsMax.y = (std::max)(mesh.boundsMax.y, v.position.y);
            mesh.boundsMax.z = (std::max)(mesh.boundsMax.z, v.position.z);
        }
    }

    // メッシュをリソースに登録
    std::vector<ModelResource::Mesh> resMeshes = {mesh};
    res->SetMeshes(resMeshes);

    // --- Materialの構築 ---
    ModelResource::Material resMat;
    resMat.name = "BatchMaterial";
    // 必要に応じてデフォルト値を設定
    resMat.baseColor                                  = {1.0f, 1.0f, 1.0f, 1.0f};
    resMat.roughness                                  = 0.8f;
    std::vector<ModelResource::Material> resMaterials = {resMat};
    res->SetMaterials(resMaterials);

    // --- Nodeの構築 ---
    ModelResource::Node resNode;
    resNode.name                              = "Root";
    resNode.parentIndex                       = -1;
    resNode.scale                             = {1, 1, 1};
    resNode.rotate                            = {0, 0, 0, 1};
    resNode.translate                         = {0, 0, 0};
    std::vector<ModelResource::Node> resNodes = {resNode};
    res->SetNodes(resNodes);

    // 全体AABB計算
    res->ComputeModelLocalAABB();

    // =================================================
    // 3. モデルインスタンスの構築 (Model)
    // コンストラクタで行われるはずの処理を手動で行う
    // =================================================

    // --- Nodeの同期 ---
    model->nodes.resize(1);
    model->nodes[0].name = "Root";
    DirectX::XMStoreFloat4x4(&model->nodes[0].localTransform, DirectX::XMMatrixIdentity());
    DirectX::XMStoreFloat4x4(&model->nodes[0].globalTransform, DirectX::XMMatrixIdentity());
    DirectX::XMStoreFloat4x4(&model->nodes[0].worldTransform, DirectX::XMMatrixIdentity());
    model->nodes[0].position = {0, 0, 0};
    model->nodes[0].scale    = {1, 1, 1};
    model->nodes[0].rotation = {0, 0, 0, 1};
    model->nodes[0].isRootOrPelvis = true; // index0=ルート。通常ロードのフラグ計算と整合させる

    // --- Materialの同期 ---
    model->materials.resize(1);
    model->materials[0].name = "BatchMaterial";

    // ★修正: 引数で渡されたマテリアルデータを共有する！
    if (baseMaterial)
    {
        model->materials[0].data = baseMaterial;
    }
    else
    {
        // マテリアルが無い場合（フォールバック）のみ、光りすぎないPBR初期値を設定する
        auto matData = std::make_shared<MaterialData>();

        matData->shaderHash = "PBR"_hash;
        matData->SetColor("materialColor"_hash, {1.0f, 1.0f, 1.0f, 1.0f});
        matData->SetColor("baseColor"_hash, {1.0f, 1.0f, 1.0f, 1.0f});
        matData->SetColor("emissiveColor"_hash, {0.0f, 0.0f, 0.0f, 1.0f});

        // PBRの「ちょうどいい」デフォルト値
        matData->SetScalar("metalness"_hash, 0.0f); // 非金属
        matData->SetScalar("roughness"_hash, 0.8f); // 光りすぎないよう、少しざらつかせる
        matData->SetScalar("occlusionStrength"_hash, 1.0f);
        matData->SetScalar("rimPower"_hash, 5.0f);
        matData->SetScalar("rimIntensity"_hash, 0.0f);

        // テクスチャは無いので全てオフ
        matData->SetScalar("useAlbedoMap"_hash, 0.0f);
        matData->SetScalar("useNormalMap"_hash, 0.0f);
        matData->SetScalar("useMetalRoughMap"_hash, 0.0f);
        matData->SetScalar("useOcclusionMap"_hash, 0.0f);

        model->materials[0].data = matData;
    }

    // =================================================================
    // ★ 追加: Model 側メッシュ（描画ループが参照する vector）を構築する
    //    OpaquePass 等は model->GetMeshes()（= Model::meshes）を走査するため、
    //    ここを埋めないとドローコールが呼ばれない。
    //    通常コンストラクタの「参照を保存」処理と同じことを行う。
    // =================================================================
    {
        // この時点で res(ModelResource) には Mesh/Material が登録済み、
        // model->nodes / model->materials も上で構築済み。
        const std::vector<ModelResource::Mesh> &resMeshes = res->GetMeshes();
        model->meshes.resize(resMeshes.size());
        for (size_t i = 0; i < resMeshes.size(); ++i)
        {
            const ModelResource::Mesh &src = resMeshes[i];
            Model::Mesh               &dst = model->meshes[i];

            // ---- 参照を保存（描画ループが mesh.data->vertexBuffer を使う）----
            dst.data = &src;

            // マテリアル参照（materialIndex 範囲チェック）
            if (src.materialIndex >= 0 &&
                src.materialIndex < (int)model->materials.size())
            {
                dst.material = &model->materials.at(src.materialIndex);
            }
            else if (!model->materials.empty())
            {
                dst.material = &model->materials.at(0);
            }
            else
            {
                dst.material = nullptr;
            }

            // ノード参照（CreateFromData はルート1ノードのみ）
            if (src.nodeIndex >= 0 && src.nodeIndex < (int)model->nodes.size())
            {
                dst.node = &model->nodes.at(src.nodeIndex);
            }
            else if (!model->nodes.empty())
            {
                dst.node = &model->nodes.at(0);
            }
            else
            {
                dst.node = nullptr;
            }

            // ボーンは無し（静的バッチメッシュ）。dst.bones は空のまま。
        }
    }

    return model;
}

int Model::GetRootMotionNodeIndex() const
{
    // ノードの中から「ルート」や「腰」にあたるボーンの名前を検索する
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        std::string name = nodes[i].name;

        // 比較しやすいように小文字に変換
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });

        // Mixamo, Unreal, Unity などの一般的なルートボーン名を網羅
        if (name.find("root") != std::string::npos ||
            name.find("hips") != std::string::npos ||
            name.find("pelvis") != std::string::npos ||
            name.find("bip01") != std::string::npos) // 3ds Max Biped用
        {
            return static_cast<int>(i);
        }
    }

    // 万が一見つからなければ安全のために 0 を返す
    return 0;
}

int Model::FindRealRootMotionNodeIndex(int animIndex) const
{
    ZoneScopedN("Model::FindRealRootMotionNodeIndex");

    if (animIndex < 0) return 0;

    // ★ キャッシュヒット: 過去に一度でも計算済みならO(1)で返す。
    //   結果はこのクリップのアニメーションデータだけで決まる静的な値なので、
    //   毎フレーム ComputeAnimation を全ノード×2回走らせる必要はない。
    if ((size_t)animIndex < _rootMotionNodeCache.size() && _rootMotionNodeCache[(size_t)animIndex] >= 0)
        return _rootMotionNodeCache[(size_t)animIndex];

    int result = 0; // 誰も動いていなければ安全に0を返す（従来仕様を維持）

    std::vector<NodePose> startPoses, endPoses;

    // 0秒時点のポーズを取得
    ComputeAnimation(animIndex, 0.0f, startPoses);

    // 少し時間が進んだ時点（0.5秒）のポーズを取得
    ComputeAnimation(animIndex, 0.5f, endPoses);

    if (!startPoses.empty() && !endPoses.empty())
    {
        // ノードは通常、親から子へ順に並んでいる (0:Root, 1:Hips, 2:Spine ...)
        for (size_t i = 0; i < std::min(startPoses.size(), endPoses.size()); ++i)
        {
            float dx     = endPoses[i].position.x - startPoses[i].position.x;
            float dz     = endPoses[i].position.z - startPoses[i].position.z;
            float distSq = dx * dx + dz * dz;

            // XZ平面で 1cm (0.01m) 以上動いている最初の親ボーンを「真のルート」と見なす！
            if (distSq > 0.0001f)
            {
                result = static_cast<int>(i); // Hipsなどの本当に動いているインデックスが確定
                break;
            }
        }
    }

    // ★ キャッシュへ格納。以降この animIndex での呼び出しは全て初回計算を再利用する。
    if ((size_t)animIndex >= _rootMotionNodeCache.size())
        _rootMotionNodeCache.resize((size_t)animIndex + 1, -1);
    _rootMotionNodeCache[(size_t)animIndex] = result;

    return result;
}

const Model::NodePose &Model::GetPinnedPoseAtZero(int animIndex, int nodeIndex) const
{
    ZoneScopedN("Model::GetPinnedPoseAtZero");

    // (animIndex, nodeIndex) を 1キーに畳み込む（どちらも非負の前提）。
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(animIndex)) << 32) |
                         static_cast<uint32_t>(nodeIndex);

    auto it = _pinnedPoseAtZeroCache.find(key);
    if (it != _pinnedPoseAtZeroCache.end())
        return it->second; // キャッシュヒット：0秒ポーズを再計算せず返す

    // 初回のみ 0秒時点のポーズを計算して格納（以降は不変なので再利用）。
    NodePose pose;
    ComputeAnimation(animIndex, nodeIndex, 0.0f, pose);
    return _pinnedPoseAtZeroCache.emplace(key, pose).first->second;
}