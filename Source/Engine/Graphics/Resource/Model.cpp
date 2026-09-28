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
#include <algorithm>
#include <cctype>
#include <tracy/Tracy.hpp>

// 大文字小文字を無視した部分一致。
//   ロード時に「ルート・Pelvis・Hips 系のボーンか」を一度だけ判定するのに使う。
//   毎フレームの処理から呼ばないこと。std::string のヒープ確保が骨の数 × フレーム分積み上がり、
//   処理落ちの大きな原因になる。
static bool ContainsCI(const std::string &name, const char *needle)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(needle) != std::string::npos;
}

// time を挟む2つのキーフレームを二分探索で探し、補間率を求める
template <typename T>
void EvaluateKeyframes(const std::vector<T> &keyframes, float time, float &outRate, const T *&outK0, const T *&outK1)
{
    if (keyframes.empty())
    {
        outK0 = outK1 = nullptr;
        return;
    }
    // キーフレームが1つか、最初のキーより前なら最初の値で止める
    if (keyframes.size() == 1 || time <= keyframes.front().seconds)
    {
        outK0 = outK1 = &keyframes.front();
        outRate       = 0.0f;
        return;
    }
    // 最後のキーより後なら最後の値で止める
    if (time >= keyframes.back().seconds)
    {
        outK0 = outK1 = &keyframes.back();
        outRate       = 0.0f;
        return;
    }

    // time 以上になる最初のキーフレームを二分探索（O(log N)）で探す
    auto it = std::lower_bound(keyframes.begin(), keyframes.end(), time,
                               [](const T &kf, float t) { return kf.seconds < t; });

    outK1 = &(*it);
    outK0 = &(*(it - 1));

    // 0 除算を避ける。UE5 などの書き出しでは、IK の切り替えや姿勢の瞬間的な変更を表すために、
    //   同じ時刻（またはごく僅かな差）に2つのキーフレームが置かれることがある。
    //   分母がほぼ 0 のまま割ると outRate が NaN / Inf になり、Slerp / Lerp に渡ってそのボーンの
    //   向きが壊れる。瞬間的な変化とみなし、後ろのキーフレームの値をそのまま使う。
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

Model::Model(ID3D11Device *device, const char *filename, float sampleRate)
    : filePath(filename ? filename : "")
{
    // 空のパスでは作らない（ResourceManager::CreateModelInstance が先に弾いている）
    if (!filename || filename[0] == '\0') return;

    resource = ResourceManager::Instance().LoadModelResource(filename);
    if (!resource.IsValid())
    {
        _ASSERT_EXPR(false, L"ModelResource が取得できませんでした。");
        return;
    }

    m_translationRetargeting = ResourceManager::Instance().GetModel(resource)->GetTranslationRetargeting();

    // 1. ModelResource のノード階層をこのインスタンスに写す
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

        // 位置をリターゲットしないボーンかを、ここで一度だけ決める（毎フレーム文字列で判定しない）。
        //   0 番（ルート）と Pelvis・Hips・Root 系のボーンだけが、アニメーションの位置の移動をそのまま使う。
        dst.isRootOrPelvis = (i == 0) ||
                             ContainsCI(src.name, "pelvis") ||
                             ContainsCI(src.name, "hips") ||
                             ContainsCI(src.name, "root");

        dst.children.clear();

        XMStoreFloat4x4(&dst.localTransform, DirectX::XMMatrixIdentity());
        XMStoreFloat4x4(&dst.globalTransform, DirectX::XMMatrixIdentity());
        XMStoreFloat4x4(&dst.worldTransform, DirectX::XMMatrixIdentity());
    }
    // 2. 親子のポインタを張る
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto &src = resNodes[i];
        if (src.parentIndex >= 0 && src.parentIndex < (int)nodes.size())
        {
            nodes[i].parent = &nodes[src.parentIndex];
            nodes[src.parentIndex].children.push_back(&nodes[i]);
        }
    }

    // 親が子より先に来る計算の順番を作る。glTF のノードは親が先に並ぶとは限らず、
    // nodes を並べ替えるとメッシュやアニメーションが持つノード番号がずれるので、順番だけ別に持つ
    m_nodeUpdateOrder.clear();
    m_nodeUpdateOrder.reserve(nodes.size());
    std::vector<int> pending;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].parent == nullptr) pending.push_back(static_cast<int>(i));
    }
    while (!pending.empty())
    {
        const int index = pending.back();
        pending.pop_back();
        m_nodeUpdateOrder.push_back(index);
        for (const Node *child : nodes[index].children)
        {
            pending.push_back(static_cast<int>(child - nodes.data()));
        }
    }

    // 3. ModelResource のマテリアルから、このインスタンスの MaterialData を作る
    const std::vector<ModelResource::Material> &resMaterials = ResourceManager::Instance().GetModel(resource)->GetMaterials();
    materials.resize(resMaterials.size());

    for (size_t i = 0; i < resMaterials.size(); ++i)
    {
        const auto &src = resMaterials[i];
        auto       &dst = materials[i];

        dst.name = src.name;
        dst.data = std::make_shared<MaterialData>();

        // シェーダーは PBR を既定にする（MaterialComponent で差し替えられる）
        dst.data->shaderHash = "PBR"_hash;

        dst.data->alphaMode   = static_cast<AlphaMode>(src.alphaMode);
        dst.data->alphaCutoff = src.alphaCutoff;

        // 色
        dst.data->SetColor("materialColor"_hash, src.baseColor); // Phong / Basic 用
        dst.data->SetColor("baseColor"_hash, src.baseColor);     // PBR 用
        // emissiveColor (float3) を float4 に変換して格納
        dst.data->SetColor("emissiveColor"_hash, {src.emissiveColor.x, src.emissiveColor.y, src.emissiveColor.z, 1.0f});

        // スカラー
        dst.data->SetScalar("metalness"_hash, src.metalness);
        dst.data->SetScalar("roughness"_hash, src.roughness);
        dst.data->SetScalar("occlusionStrength"_hash, src.occlusionStrength);
        dst.data->SetScalar("alphaCutoff"_hash, src.alphaCutoff);

        // Toon シェーダー用の既定値（無いと黒くなる）
        dst.data->SetColor("rimColor"_hash, {1.0f, 1.0f, 1.0f, 1.0f});
        dst.data->SetScalar("rimPower"_hash, 5.0f);
        dst.data->SetScalar("rimIntensity"_hash, 1.0f);

        // Toon 用の既定のランプテクスチャ
        dst.data->SetTexture("RampTexture"_hash, ResourceManager::Instance().LoadTexture("Assets/Textures/Shader/ramp.png"));

        // Outline シェーダー用の既定値
        dst.data->SetColor("color"_hash, {0.0f, 0.0f, 0.0f, 1.0f}); // 線の色
        dst.data->SetScalar("size"_hash, 0.02f);                    // 線の太さ

        // テクスチャ。シェーダーごとにスロット名が違うので、同じテクスチャを複数の名前で登録する
        if (src.baseMap)
        {
            auto handle = ResourceManager::Instance().RegisterTexture(src.baseMap);
            dst.data->SetTexture("DiffuseMap"_hash, handle);
            dst.data->SetTexture("AlbedoMap"_hash, handle);
            dst.data->SetScalar("useAlbedoMap"_hash, 1.0f);
        }
        else
        {
            dst.data->SetScalar("useAlbedoMap"_hash, 0.0f);
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
            dst.data->SetScalar("useEmissiveMap"_hash, 1.0f);
        }
        else
        {
            dst.data->SetScalar("useEmissiveMap"_hash, 0.0f);
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
            dst.data->SetScalar("useMetalRoughMap"_hash, 1.0f);
        }
        else
        {
            dst.data->SetScalar("useMetalRoughMap"_hash, 0.0f);
        }
    }

    // 4. メッシュ・マテリアル・ノード・ボーンをポインタでつなぐ
    const std::vector<ModelResource::Mesh> &resMeshes = ResourceManager::Instance().GetModel(resource)->GetMeshes();
    meshes.resize(resMeshes.size());
    for (size_t i = 0; i < resMeshes.size(); ++i)
    {
        const auto &src = resMeshes[i];
        auto       &dst = meshes[i];

        dst.data     = &src;
        dst.material = &materials.at(src.materialIndex);

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

    // 5. 単位行列を基準に、ノードの行列を一度求めておく
    DirectX::XMFLOAT4X4 worldTransform;
    DirectX::XMStoreFloat4x4(&worldTransform, DirectX::XMMatrixIdentity());
    UpdateTransform(worldTransform);
}

int Model::GetAnimationIndex(const char *name) const
{
    if (!resource.IsValid())
    {
        return -1;
    }

    // 最初の呼び出しで名前 → 番号の表を作る（ロード後は変わらないので使い回す）
    if (!m_animIndexBuilt)
    {
        const auto &animations = ResourceManager::Instance().GetModel(resource)->GetAnimations();
        m_animIndexCache.clear();
        m_animIndexCache.reserve(animations.size());
        for (size_t i = 0; i < animations.size(); ++i)
        {
            // 同じ名前があれば、先に見つかった方を使う
            m_animIndexCache.emplace(CCL::Utils::HashString(animations[i].name), static_cast<int>(i));
        }
        m_animIndexBuilt = true;
    }

    auto it = m_animIndexCache.find(CCL::Utils::HashString(name));
    return (it != m_animIndexCache.end()) ? it->second : -1;
}

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

void Model::UpdateTransform(const DirectX::XMFLOAT4X4 &worldTransform)
{
    DirectX::XMMATRIX ParentWorldTransform = DirectX::XMLoadFloat4x4(&worldTransform);

    for (int nodeIndex : m_nodeUpdateOrder)
    {
        Node &node = nodes[nodeIndex];

        // 親ノードに対する行列
        DirectX::XMMATRIX S              = DirectX::XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z);
        DirectX::XMMATRIX R              = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.rotation));
        DirectX::XMMATRIX T              = DirectX::XMMatrixTranslation(node.position.x, node.position.y, node.position.z);
        DirectX::XMMATRIX LocalTransform = S * R * T;

        // モデルの原点に対する行列。m_nodeUpdateOrder の順に回すので、親の globalTransform は求め終わっている
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

        // ワールド行列
        DirectX::XMMATRIX WorldTransform = GlobalTransform * ParentWorldTransform;

        DirectX::XMStoreFloat4x4(&node.localTransform, LocalTransform);
        DirectX::XMStoreFloat4x4(&node.globalTransform, GlobalTransform);
        DirectX::XMStoreFloat4x4(&node.worldTransform, WorldTransform);
    }
}

void Model::ComputeNodePose(const ModelResource::Animation &animation, int nodeIndex, float time, NodePose &nodePose) const
{
    // ここに Tracy のゾーンは置かない。ノードの数だけ（キャラ1体で 77〜118 回）毎フレーム呼ばれるので、
    // ゾーンを出すコストで測りたい時間そのものが膨らむ。呼び出し元の ComputeAnimation の1ゾーンで足りる。

    const ModelResource::NodeAnim &nodeAnim = animation.nodeAnims.at(nodeIndex);

    // 先にバインドポーズ（モデルの元の姿勢）を入れておく。
    // キーフレームが無いチャンネルが 0 になるのを防ぐ
    const Node &bindNode = nodes.at(nodeIndex);
    nodePose.position    = bindNode.position;
    nodePose.rotation    = bindNode.rotation;
    nodePose.scale       = bindNode.scale;

    float rate;

    // --- 位置 ---
    //
    // 位置のリターゲット（体型の崩れを防ぐ）。
    //   ルート・Pelvis・Hips 系以外は、アニメーションの位置の移動を使わず、モデルの骨の長さ
    //   （bindNode.position）を使う。対象かどうかはロード時に決めた isRootOrPelvis を見るだけにする。
    //   ここで骨の名前から判定し直してはいけない（小文字化と部分一致が毎フレーム × 骨の数だけ走る）。
    //
    //   対象外のノードでは結果を捨てるので、計算自体を飛ばす。isRootOrPelvis が true なのは
    //   リグ全体で1〜3本だけなので、位置のキーフレーム探索と Lerp はほとんどのノードで省ける。
    //   nodePose.position は上でバインドポーズを入れてあるので、else は要らない。
    //
    // リターゲットを切っているモデルでは、全ボーンの移動をそのまま使う。
    //   モデルとアニメーションの骨格が同じであることが前提。スライドするボーン
    //   （Muriel の wing_blade_track など）は移動そのものが演出なので、
    //   バインドポーズで上書きすると、その下のボーンごと位置が崩れる。
    if (bindNode.isRootOrPelvis || !m_translationRetargeting)
    {
        const ModelResource::VectorKeyframe *posK0, *posK1;
        EvaluateKeyframes(nodeAnim.positionKeyframes, time, rate, posK0, posK1);
        if (posK0 && posK1)
        {
            DirectX::XMVECTOR V0 = DirectX::XMLoadFloat3(&posK0->value);
            DirectX::XMVECTOR V1 = DirectX::XMLoadFloat3(&posK1->value);
            DirectX::XMVECTOR V  = DirectX::XMVectorLerp(V0, V1, rate);
            DirectX::XMStoreFloat3(&nodePose.position, V);
        }
    }

    // --- 回転 ---
    const ModelResource::QuaternionKeyframe *rotK0, *rotK1;
    EvaluateKeyframes(nodeAnim.rotationKeyframes, time, rate, rotK0, rotK1);
    if (rotK0 && rotK1)
    {
        DirectX::XMVECTOR Q0 = DirectX::XMLoadFloat4(&rotK0->value);
        DirectX::XMVECTOR Q1 = DirectX::XMLoadFloat4(&rotK1->value);
        DirectX::XMVECTOR Q  = DirectX::XMQuaternionSlerp(Q0, Q1, rate);
        DirectX::XMStoreFloat4(&nodePose.rotation, Q);
    }

    // --- スケール ---
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

void Model::ComputeAnimation(int animationIndex, float time, std::vector<NodePose> &nodePoses) const
{
    ZoneScopedN("Model::ComputeAnimation");

    if (!resource.IsValid()) return;

    if (nodePoses.size() != nodes.size())
    {
        nodePoses.resize(nodes.size());
    }

    // ResourceManager と Animation はループの外で1回だけ引く。ループ内で引き直さないこと
    const auto                     &animations = ResourceManager::Instance().GetModel(resource)->GetAnimations();
    const ModelResource::Animation &animation  = animations.at(animationIndex);

    for (size_t nodeIndex = 0; nodeIndex < nodePoses.size(); ++nodeIndex)
    {
        ComputeNodePose(animation, static_cast<int>(nodeIndex), time, nodePoses.at(nodeIndex));
    }
}

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

void Model::GetNodePoses(std::vector<NodePose> &nodePoses) const
{
    nodePoses.resize(nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
    {
        const Node &node = nodes[nodeIndex];
        NodePose   &pose = nodePoses[nodeIndex];

        pose.position = node.position;
        pose.rotation = node.rotation;
        pose.scale    = node.scale;
    }
}

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

        // 回転の球面線形補間
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

int Model::FindRealRootMotionNodeIndex(int animIndex) const
{
    ZoneScopedN("Model::FindRealRootMotionNodeIndex");

    if (animIndex < 0) return 0;

    // 一度求めていれば、キャッシュから返す
    if ((size_t)animIndex < m_rootMotionNodeCache.size() && m_rootMotionNodeCache[(size_t)animIndex] >= 0)
        return m_rootMotionNodeCache[(size_t)animIndex];

    int result = 0; // どのボーンも動いていなければ 0（ルート）

    std::vector<NodePose> startPoses, endPoses;

    // 0秒と 0.5秒の姿勢を比べる
    ComputeAnimation(animIndex, 0.0f, startPoses);
    ComputeAnimation(animIndex, 0.5f, endPoses);

    if (!startPoses.empty() && !endPoses.empty())
    {
        // ノードは親から子の順に並んでいる前提（0: Root, 1: Hips, 2: Spine ...）
        for (size_t i = 0; i < std::min(startPoses.size(), endPoses.size()); ++i)
        {
            float dx     = endPoses[i].position.x - startPoses[i].position.x;
            float dz     = endPoses[i].position.z - startPoses[i].position.z;
            float distSq = dx * dx + dz * dz;

            // XZ 平面で 1cm（0.01m）より大きく動いた、最も親に近いボーンをルートとみなす
            if (distSq > 0.0001f)
            {
                result = static_cast<int>(i);
                break;
            }
        }
    }

    // キャッシュに入れる
    if ((size_t)animIndex >= m_rootMotionNodeCache.size())
        m_rootMotionNodeCache.resize((size_t)animIndex + 1, -1);
    m_rootMotionNodeCache[(size_t)animIndex] = result;

    return result;
}

const Model::NodePose &Model::GetPinnedPoseAtZero(int animIndex, int nodeIndex) const
{
    ZoneScopedN("Model::GetPinnedPoseAtZero");

    // (animIndex, nodeIndex) を1つのキーにまとめる（どちらも 0 以上の前提）
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(animIndex)) << 32) |
                         static_cast<uint32_t>(nodeIndex);

    auto it = m_pinnedPoseAtZeroCache.find(key);
    if (it != m_pinnedPoseAtZeroCache.end())
        return it->second;

    // 初回だけ0秒の姿勢を求めて入れる
    NodePose pose;
    ComputeAnimation(animIndex, nodeIndex, 0.0f, pose);
    return m_pinnedPoseAtZeroCache.emplace(key, pose).first->second;
}
