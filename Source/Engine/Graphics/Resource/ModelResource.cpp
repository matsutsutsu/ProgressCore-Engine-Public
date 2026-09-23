#include <stdlib.h>
#include <fstream>
#include <functional>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <filesystem>
#include <algorithm>
#include <unordered_set>
#include <json.hpp>
#include "GLTFImporter.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ResourceManager.h" // テクスチャSRVのパス単位キャッシュ
#include <winsock.h>

#include "ModelResource.h"

const std::vector<D3D11_INPUT_ELEMENT_DESC> ModelResource::InputElementDescs =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BONE_WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BONE_INDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// CEREALバージョン定義
CEREAL_CLASS_VERSION(ModelResource::Node, 1)
CEREAL_CLASS_VERSION(ModelResource::Material, 1)
CEREAL_CLASS_VERSION(ModelResource::Subset, 1)
CEREAL_CLASS_VERSION(ModelResource::Vertex, 1)
CEREAL_CLASS_VERSION(ModelResource::Mesh, 1)
CEREAL_CLASS_VERSION(ModelResource::VectorKeyframe, 1)
CEREAL_CLASS_VERSION(ModelResource::QuaternionKeyframe, 1)
CEREAL_CLASS_VERSION(ModelResource::NodeAnim, 1)
CEREAL_CLASS_VERSION(ModelResource::Animation, 1)
CEREAL_CLASS_VERSION(ModelResource, 1)

// シリアライズ
namespace DirectX
{
    template <class Archive>
    void serialize(Archive &archive, XMUINT4 &v)
    {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w));
    }

    template <class Archive>
    void serialize(Archive &archive, XMFLOAT2 &v)
    {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y));
    }

    template <class Archive>
    void serialize(Archive &archive, XMFLOAT3 &v)
    {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z));
    }

    template <class Archive>
    void serialize(Archive &archive, XMFLOAT4 &v)
    {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w));
    }

    template <class Archive>
    void serialize(Archive &archive, XMFLOAT4X4 &m)
    {
        archive(
            cereal::make_nvp("_11", m._11), cereal::make_nvp("_12", m._12), cereal::make_nvp("_13", m._13), cereal::make_nvp("_14", m._14),
            cereal::make_nvp("_21", m._21), cereal::make_nvp("_22", m._22), cereal::make_nvp("_23", m._23), cereal::make_nvp("_24", m._24),
            cereal::make_nvp("_31", m._31), cereal::make_nvp("_32", m._32), cereal::make_nvp("_33", m._33), cereal::make_nvp("_34", m._34),
            cereal::make_nvp("_41", m._41), cereal::make_nvp("_42", m._42), cereal::make_nvp("_43", m._43), cereal::make_nvp("_44", m._44));
    }
} // namespace DirectX

template <class Archive>
void ModelResource::Node::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(id),
        CEREAL_NVP(name),
        CEREAL_NVP(path),
        CEREAL_NVP(parentIndex),
        CEREAL_NVP(scale),
        CEREAL_NVP(rotate),
        CEREAL_NVP(translate));
}

template <class Archive>
void ModelResource::Material::serialize(Archive &ar, int version)
{
    ar(CEREAL_NVP(name),
       CEREAL_NVP(baseTextureFileName),
       CEREAL_NVP(normalTextureFileName),
       CEREAL_NVP(emissiveTextureFileName),
       CEREAL_NVP(occlusionTextureFileName),
       CEREAL_NVP(metalnessRoughnessTextureFileName),
       CEREAL_NVP(baseColor),
       CEREAL_NVP(emissiveColor),
       CEREAL_NVP(metalness),
       CEREAL_NVP(roughness),
       CEREAL_NVP(occlusionStrength),
       CEREAL_NVP(alphaCutoff),
       CEREAL_NVP(alphaMode));
}

template <class Archive>
void ModelResource::Subset::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(startIndex),
        CEREAL_NVP(indexCount),
        CEREAL_NVP(materialIndex));
}

template <class Archive>
void ModelResource::Vertex::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(position),
        CEREAL_NVP(normal),
        CEREAL_NVP(tangent),
        CEREAL_NVP(texcoord),
        CEREAL_NVP(boneWeight),
        CEREAL_NVP(boneIndex));
}

template <class Archive>
void ModelResource::Bone::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(nodeIndex),
        CEREAL_NVP(offsetTransform));
}

template <class Archive>
void ModelResource::Mesh::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(vertices),
        CEREAL_NVP(indices),
        CEREAL_NVP(subsets),
        CEREAL_NVP(bones),
        CEREAL_NVP(nodeIndex),
        CEREAL_NVP(nodeIndices),
        CEREAL_NVP(materialIndex),
        CEREAL_NVP(offsetTransforms),
        CEREAL_NVP(boundsMin),
        CEREAL_NVP(boundsMax));
}

template <class Archive>
void ModelResource::VectorKeyframe::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(seconds),
        CEREAL_NVP(value));
}

template <class Archive>
void ModelResource::QuaternionKeyframe::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(seconds),
        CEREAL_NVP(value));
}

template <class Archive>
void ModelResource::NodeAnim::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(positionKeyframes),
        CEREAL_NVP(rotationKeyframes),
        CEREAL_NVP(scaleKeyframes));
}

template <class Archive>
void ModelResource::Animation::serialize(Archive &archive, int version)
{
    archive(
        CEREAL_NVP(name),
        CEREAL_NVP(secondsLength),
        CEREAL_NVP(nodeAnims));
}

// 読み込み
// cerealがあればDeserializeで読み込み、
// なければ汎用GLTFImporterで読み込み
void ModelResource::Load(ID3D11Device *device, const char *filename)
{
    // ディレクトリパス取得
    char drive[32], dir[256], dirname[256];
    ::_splitpath_s(filename, drive, sizeof(drive), dir, sizeof(dir), nullptr, 0, nullptr, 0);
    ::_makepath_s(dirname, sizeof(dirname), drive, dir, nullptr, nullptr);

    std::filesystem::path filepath(filename);
    std::filesystem::path dirpath(filepath.parent_path());
    std::filesystem::path extension = filepath.extension();

    // モデル定義ファイル（.model.json）は本体モデルと追加アニメーションを
    // 束ねた別形式なので、専用の経路に丸ごと委譲する。
    // 既存の .glb / .gltf / .cereal の読み込みには一切触れない。
    if (IsModelSetFile(filename))
    {
        LoadModelSet(device, filename);
        return;
    }

    //// 独自形式のモデルファイルの存在確認
    filepath.replace_extension(".cereal");
    if (std::filesystem::exists(filepath))
    {
        // 独自形式のモデルファイルの読み込み
        Deserialize(filepath.string().c_str());
    }
    else if (extension == ".gltf" || extension == ".glb")
    {
        // 汎用モデルファイルの読み込み
        // GLTFImporterを使用してモデルデータをリソース用に読み込む
        // これはデータをCPUメモリ上に読み込むだけで、GPUリソースは作成しない
        GLTFImporter importer(filename);

        // マテリアルデータ読み取り
        importer.LoadMaterials(materials, nullptr);

        // ノードデータ読み取り
        importer.LoadNodes(nodes);

        // メッシュデータ読み取り
        importer.LoadMeshes(meshes, nodes);

        // アニメーションデータ読み取り
        importer.LoadAnimations(animations, nodes, 30.0f); // 第3引数はサンプリングレート(fps)

        //独自形式のモデルファイルを保存
        Serialize(filepath.string().c_str());
    }
    else
    {
        _ASSERT_EXPR_A(false, "found not model file");
    }

    // モデル構築
    BuildModel(device, dirname);

    // モデル全体のAABBを計算
    ComputeModelLocalAABB();
}

// ============================================================================
// モデル定義ファイル（.model.json）
//   本体モデル1つと、別ファイルのアニメーションを1枚のJSONで束ねる形式。
//
//     {
//         "model": "Muriel.glb"
//     }
//
//   パスはこのJSONが置かれたディレクトリからの相対で書く。
//
//   "animations" の書き方は3通り。
//     省略   … 既定の "Animations" フォルダを走査する（推奨）
//     文字列 … 指定したフォルダを走査する
//     配列   … 書いたファイルだけを、書いた順に読む
//   走査にしておけば、アニメーションを増やすたびにこのJSONを編集しなくてよい。
//   アニメーションをモデルへ焼き込まず別ファイルのまま扱えるようにするための形式で、
//   従来どおり .glb を直接指定する使い方もそのまま残っている。
// ============================================================================
bool ModelResource::IsModelSetFile(const char *filename)
{
    if (filename == nullptr) return false;

    std::string path(filename);
    std::transform(path.begin(), path.end(), path.begin(), tolower); // 小文字化

    const std::string suffix = ".model.json";
    if (path.size() < suffix.size()) return false;

    return path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// モデル定義ファイルの読み込み
void ModelResource::LoadModelSet(ID3D11Device *device, const char *filename)
{
    const std::filesystem::path jsonPath(filename);
    const std::filesystem::path baseDir = jsonPath.parent_path();

    // ---- 定義の読み取り ----
    const std::string text = FileSystem::ReadAllText(jsonPath.string());
    if (text.empty())
    {
        CCL_LOG_ERROR(LogCategory::Render, "モデル定義ファイルを読み込めません: %s", filename);
        return;
    }

    std::string              modelRelative;
    std::vector<std::string> animationRelatives;
    std::string              animationDirRelative;      // 空でなければフォルダ走査
    bool                     useTranslationRetargeting = true;
    try
    {
        const nlohmann::json j = nlohmann::json::parse(text);
        modelRelative          = j.at("model").get<std::string>();

        // 配列のときだけ明示列挙。それ以外（文字列 / 省略）はフォルダ走査にする。
        if (j.contains("animations") && j.at("animations").is_array())
        {
            animationRelatives = j.at("animations").get<std::vector<std::string>>();
        }
        else if (j.contains("animations") && j.at("animations").is_string())
        {
            animationDirRelative = j.at("animations").get<std::string>();
        }
        else
        {
            animationDirRelative = "Animations";
        }
        if (j.contains("translationRetargeting"))
        {
            useTranslationRetargeting = j.at("translationRetargeting").get<bool>();
        }
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Render, "モデル定義ファイルの解析に失敗しました: %s (%s)", filename, e.what());
        return;
    }

    // 参照先はJSONからの相対パスとして解決する
    const std::filesystem::path        modelPath = baseDir / modelRelative;
    std::vector<std::filesystem::path> animationPaths;
    std::filesystem::path              animationDir; // 走査したフォルダ（鮮度判定で使う）

    if (!animationDirRelative.empty())
    {
        animationDir = baseDir / animationDirRelative;
        if (std::filesystem::is_directory(animationDir))
        {
            for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(animationDir))
            {
                if (!entry.is_regular_file()) continue;

                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), tolower);
                if (extension != ".gltf" && extension != ".glb") continue;

                animationPaths.emplace_back(entry.path());
            }

            // directory_iterator の順序はファイルシステム依存なので並べ直す。
            // 照合は名前で行うため順序は動作に影響しないが、
            // .cereal の中身が実行のたびに入れ替わるのを防ぐ。
            std::sort(animationPaths.begin(), animationPaths.end());
        }
    }
    else
    {
        animationPaths.reserve(animationRelatives.size());
        for (const std::string &relative : animationRelatives)
        {
            animationPaths.emplace_back(baseDir / relative);
        }
    }

    if (!std::filesystem::exists(modelPath))
    {
        CCL_LOG_ERROR(LogCategory::Render, "本体モデルが見つかりません: %s", modelPath.string().c_str());
        return;
    }

    // ---- キャッシュの鮮度判定 ----
    // .glb を直接指定する経路は「.cereal があれば無条件に採用」だが、
    // この形式はアニメーションを何度も差し替えながら使うことを想定している。
    // 元ファイルの方が新しければ作り直しておかないと、書き出したのに
    // 反映されない、という原因の分かりにくい足止めが起きる。
    std::filesystem::path cachePath = jsonPath;
    cachePath.replace_extension(".cereal"); // Muriel.model.json → Muriel.model.cereal

    bool cacheUsable = std::filesystem::exists(cachePath);
    if (cacheUsable)
    {
        const std::filesystem::file_time_type cacheTime = std::filesystem::last_write_time(cachePath);

        // キャッシュより新しい元ファイルが1つでもあれば作り直す
        auto isNewerThanCache = [&cacheTime](const std::filesystem::path &path) {
            std::error_code                       ec;
            const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, ec);
            return !ec && time > cacheTime;
        };

        cacheUsable = !isNewerThanCache(jsonPath) && !isNewerThanCache(modelPath);

        // フォルダ走査のときはフォルダ自体の更新時刻も見る。
        // ファイルの追加・削除は個々の mtime には現れないが、
        // 親ディレクトリの mtime は動くのでこれで増減を検知できる。
        if (cacheUsable && !animationDir.empty())
        {
            cacheUsable = !isNewerThanCache(animationDir);
        }

        for (const std::filesystem::path &animationPath : animationPaths)
        {
            if (!cacheUsable) break;
            cacheUsable = !isNewerThanCache(animationPath);
        }
    }

    if (cacheUsable)
    {
        Deserialize(cachePath.string().c_str());
    }
    else
    {
        // ---- 本体モデル ----
        {
            GLTFImporter importer(modelPath.string().c_str());
            importer.LoadMaterials(materials, nullptr);
            importer.LoadNodes(nodes);
            importer.LoadMeshes(meshes, nodes);
            // 本体側にアニメーションが含まれていればそれも採用する
            importer.LoadAnimations(animations, nodes, 30.0f);
        }

        // ---- 追加アニメーション ----
        for (const std::filesystem::path &animationPath : animationPaths)
        {
            AppendAnimations(animationPath.string().c_str());
        }

        Serialize(cachePath.string().c_str());
    }

    // 位置リターゲットの設定は cereal に保存していないため、
    // キャッシュから復元した場合も含めて毎回ここで反映する。
    SetTranslationRetargeting(useTranslationRetargeting);

    // テクスチャの探索起点は「定義ファイル」ではなく「本体モデル」の置き場所にする。
    // glb の埋め込みテクスチャは GLTFImporter が glb と同じ階層の Textures/ へ
    // 展開するため、ここを合わせておけば定義ファイルを別フォルダに置いても壊れない。
    char modelDrive[32], modelDir[256], modelDirName[256];
    const std::string modelPathString = modelPath.string();
    ::_splitpath_s(modelPathString.c_str(), modelDrive, sizeof(modelDrive), modelDir, sizeof(modelDir), nullptr, 0, nullptr, 0);
    ::_makepath_s(modelDirName, sizeof(modelDirName), modelDrive, modelDir, nullptr, nullptr);

    // モデル構築
    BuildModel(device, modelDirName);

    // モデル全体のAABBを計算
    ComputeModelLocalAABB();
}

// アニメーション追加読み込み
void ModelResource::AppendAnimations(const char *filename)
{
    const std::filesystem::path filepath(filename);
    const std::filesystem::path extension = filepath.extension();

    if (extension != ".gltf" && extension != ".glb")
    {
        CCL_LOG_ERROR(LogCategory::Render, "アニメーションの形式が未対応です: %s", filename);
        return;
    }
    if (!std::filesystem::exists(filepath))
    {
        CCL_LOG_ERROR(LogCategory::Render, "アニメーションファイルが見つかりません: %s", filename);
        return;
    }

    GLTFImporter importer(filename);

    // GLTFImporter::LoadAnimations はボーン名でモデル側へ対応付けるので、
    // ノードの並びが違っていても正しく適用される。
    // ただし骨格そのものが別物だと全チャンネルが適用先を失い、
    // 「読めたのに動かない」という分かりにくい結果になる。
    // フォルダ走査では無関係のファイルが紛れ込むこともあるため、
    // 名前がどれだけ重なっているかを先に見ておく。
    std::vector<Node> animationNodes;
    importer.LoadNodes(animationNodes);

    std::unordered_set<std::string> modelNodeNames;
    modelNodeNames.reserve(nodes.size());
    for (const Node &node : nodes)
    {
        if (!node.name.empty()) modelNodeNames.insert(node.name);
    }

    // 名無しノード（glTF のルートなど）は空名同士で何にでも一致して
    // しまうので、数えるのは名前のあるノードだけにする。
    size_t      namedNodeCount   = 0;
    size_t      matchedNodeCount = 0;
    std::string firstUnmatchedName;
    for (const Node &node : animationNodes)
    {
        if (node.name.empty()) continue;
        ++namedNodeCount;

        if (modelNodeNames.count(node.name) > 0)
        {
            ++matchedNodeCount;
        }
        else if (firstUnmatchedName.empty())
        {
            firstUnmatchedName = node.name;
        }
    }

    if (matchedNodeCount == 0)
    {
        CCL_LOG_ERROR(LogCategory::Render,
                      "ボーン名がモデルと一つも一致しません。別の骨格のアニメーションの可能性があります: %s",
                      filename);
        return;
    }
    if (matchedNodeCount < namedNodeCount)
    {
        CCL_LOG_WARN(LogCategory::Render,
                     "モデルに存在しないボーンが %zu / %zu 本あります: %s (例 \"%s\")",
                     namedNodeCount - matchedNodeCount, namedNodeCount, filename, firstUnmatchedName.c_str());
    }

    // アニメーションデータ読み取り
    importer.LoadAnimations(animations, nodes);
}

void ModelResource::ComputeMeshBounds(Mesh &mesh)
{
    using namespace DirectX;

    XMFLOAT3 minV = {+FLT_MAX, +FLT_MAX, +FLT_MAX};
    XMFLOAT3 maxV = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (const Vertex &v : mesh.vertices)
    {
        minV.x = (std::min)(minV.x, v.position.x);
        minV.y = (std::min)(minV.y, v.position.y);
        minV.z = (std::min)(minV.z, v.position.z);

        maxV.x = (std::max)(maxV.x, v.position.x);
        maxV.y = (std::max)(maxV.y, v.position.y);
        maxV.z = (std::max)(maxV.z, v.position.z);
    }

    mesh.boundsMin = minV;
    mesh.boundsMax = maxV;
}

// モデル構築
// 実際にGPUリソースを作成する場所
void ModelResource::BuildModel(ID3D11Device *device, const char *dirname)
{
    for (Material &material : materials)
    {
        // 共通テクスチャローダ
        auto loadTexture = [&](const std::string                                &textureFileName,
                               Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &outSRV) {
            if (textureFileName.empty())
            {
                // ダミーテクスチャを作成
                HRESULT hr = GpuResourceUtils::CreateDummyTexture(device, 0xFFFFFFFF, outSRV.GetAddressOf());
                _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
                return;
            }

            // パス解決
            char fullpath[256];
            ::_makepath_s(fullpath, 256, nullptr, dirname, textureFileName.c_str(), nullptr);

            // 同名の .dds があればそれを優先（PNG参照のまま、圧縮済みDDSへ透過的に差し替え）。
            // texconv で生成した BCn/DDS を隣に置くだけでVRAM削減＆sRGB正常化が効く。
            {
                char drive[8], dir[256], fname[256], ext[64];
                ::_splitpath_s(fullpath, drive, dir, fname, ext);
                char ddspath[256];
                ::_makepath_s(ddspath, 256, drive, dir, fname, ".dds");
                if (::GetFileAttributesA(ddspath) != INVALID_FILE_ATTRIBUTES)
                    ::strcpy_s(fullpath, ddspath);
            }

            // 実際にテクスチャをロード。
            // GpuResourceUtils を直に叩くとモデルごとに毎回デコードが走るため、
            // パス単位でSRVをキャッシュする ResourceManager を経由する。
            // 同じテクスチャを複数モデルが参照していても実デコードは1回で済む。
            // （BuildModel は ResourceManager::LoadModelResource の「ロック外」区間から
            //   呼ばれる契約なので、ここで LoadTexture を呼んでも自己デッドロックしない）
            ResourceManager &resourceManager = ResourceManager::Instance();
            TextureHandle    handle          = resourceManager.LoadTexture(fullpath);
            if (ID3D11ShaderResourceView *srv = resourceManager.GetTexture(handle))
            {
                outSRV = srv; // ComPtr の代入で AddRef されるので寿命は独立
            }
            else
            {
                // ロード失敗時は白テクスチャを代用
                HRESULT hr = GpuResourceUtils::CreateDummyTexture(device, 0xFFFFFFFF, outSRV.GetAddressOf());
                _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
            }
        };

        // 各マップをロード
        loadTexture(material.baseTextureFileName, material.baseMap);
        loadTexture(material.normalTextureFileName, material.normalMap);
        loadTexture(material.emissiveTextureFileName, material.emissiveMap);
        loadTexture(material.occlusionTextureFileName, material.occlusionMap);
        loadTexture(material.metalnessRoughnessTextureFileName, material.metalnessRoughnessMap);
    }

    for (Mesh &mesh : meshes)
    {
        // サブセット
        for (Subset &subset : mesh.subsets)
        {
            subset.material = &materials.at(subset.materialIndex);
        }

        // 頂点バッファ
        {
            D3D11_BUFFER_DESC      bufferDesc      = {};
            D3D11_SUBRESOURCE_DATA subresourceData = {};

            bufferDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * mesh.vertices.size());
            //bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.Usage                 = D3D11_USAGE_IMMUTABLE;
            bufferDesc.BindFlags             = D3D11_BIND_VERTEX_BUFFER;
            bufferDesc.CPUAccessFlags        = 0;
            bufferDesc.MiscFlags             = 0;
            bufferDesc.StructureByteStride   = 0;
            subresourceData.pSysMem          = mesh.vertices.data();
            subresourceData.SysMemPitch      = 0;
            subresourceData.SysMemSlicePitch = 0;

            /*HRESULT hr = device->CreateBuffer(&bufferDesc, &subresourceData, mesh.vertexBuffer.GetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));*/

            HRESULT hr = device->CreateBuffer(&bufferDesc, &subresourceData, mesh.vertexBuffer.GetAddressOf());
            if (FAILED(hr))
            {
                printf(" VertexBuffer creation failed: hr=0x%08X, vertexCount=%zu\n", hr, mesh.vertices.size());
                mesh.vertexBuffer.Reset(); // 安全にnullへ
            }
            else
            {
                auto d = mesh.vertices.size();
                auto e = bufferDesc.ByteWidth;
                int  a = 1;
            }
        }

        // インデックスバッファ
        {
            D3D11_BUFFER_DESC      bufferDesc      = {};
            D3D11_SUBRESOURCE_DATA subresourceData = {};

            bufferDesc.ByteWidth = static_cast<UINT>(sizeof(u_int) * mesh.indices.size());
            //bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.Usage                 = D3D11_USAGE_IMMUTABLE;
            bufferDesc.BindFlags             = D3D11_BIND_INDEX_BUFFER;
            bufferDesc.CPUAccessFlags        = 0;
            bufferDesc.MiscFlags             = 0;
            bufferDesc.StructureByteStride   = 0;
            subresourceData.pSysMem          = mesh.indices.data();
            subresourceData.SysMemPitch      = 0; //Not use for index buffers.
            subresourceData.SysMemSlicePitch = 0; //Not use for index buffers.
            HRESULT hr                       = device->CreateBuffer(&bufferDesc, &subresourceData, mesh.indexBuffer.GetAddressOf());
            _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        }

        // バウンディング計算
        ComputeMeshBounds(mesh);
    }
}

// モデル全体のAABBを計算
void ModelResource::ComputeModelLocalAABB()
{
    using namespace DirectX;

    XMFLOAT3 minP = {+FLT_MAX, +FLT_MAX, +FLT_MAX};
    XMFLOAT3 maxP = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    bool hasMesh = false;
    for (const Mesh &mesh : meshes)
    {
        if (mesh.vertices.empty() && mesh.indices.empty())
            continue;

        hasMesh = true;

        minP.x = (std::min)(minP.x, mesh.boundsMin.x);
        minP.y = (std::min)(minP.y, mesh.boundsMin.y);
        minP.z = (std::min)(minP.z, mesh.boundsMin.z);

        maxP.x = (std::max)(maxP.x, mesh.boundsMax.x);
        maxP.y = (std::max)(maxP.y, mesh.boundsMax.y);
        maxP.z = (std::max)(maxP.z, mesh.boundsMax.z);
    }

    if (hasMesh)
    {
        m_boundingBox.Center = XMFLOAT3((minP.x + maxP.x) * 0.5f,
                                        (minP.y + maxP.y) * 0.5f,
                                        (minP.z + maxP.z) * 0.5f);

        m_boundingBox.Extents = XMFLOAT3((maxP.x - minP.x) * 0.5f,
                                         (maxP.y - minP.y) * 0.5f,
                                         (maxP.z - minP.z) * 0.5f);
    }
    else
    {
        m_boundingBox.Center  = XMFLOAT3(0.0f, 0.0f, 0.0f);
        m_boundingBox.Extents = XMFLOAT3(0.0f, 0.0f, 0.0f);
    }
}

// シリアライズ
void ModelResource::Serialize(const char *filename)
{
    std::ofstream ostream(filename, std::ios::binary);
    if (ostream.is_open())
    {
        cereal::BinaryOutputArchive archive(ostream);

        try
        {
            archive(
                CEREAL_NVP(nodes),
                CEREAL_NVP(materials),
                CEREAL_NVP(meshes),
                CEREAL_NVP(animations));
        }
        catch (...)
        {
            char buffer[256];
            sprintf_s(buffer, sizeof(buffer), "model serialize failed.\n%s\n", filename);
            _ASSERT_EXPR_A(false, buffer);
            return;
        }
    }
}

// デシリアライズ
void ModelResource::Deserialize(const char *filename)
{
    std::ifstream istream(filename, std::ios::binary);
    if (istream.is_open())
    {
        cereal::BinaryInputArchive archive(istream);

        try
        {
            archive(
                CEREAL_NVP(nodes),
                CEREAL_NVP(materials),
                CEREAL_NVP(meshes),
                CEREAL_NVP(animations));
        }
        catch (...)
        {
            char buffer[256];
            sprintf_s(buffer, sizeof(buffer), "model deserialize failed.\n%s\n", filename);
            _ASSERT_EXPR_A(false, buffer);
            return;
        }
    }
    else
    {
        char buffer[256];
        sprintf_s(buffer, sizeof(buffer), "File not found > %s", filename);
        _ASSERT_EXPR_A(false, buffer);
    }
}
