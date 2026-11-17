#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <vector>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// =====================================================================
// シリアライズ
//   ハンドル（overrideMaterials）は実行時だけ有効なので、スロットごとの .mat.json のパス（overridePaths）を保存して読み直す
//   ComponentSerializer<T> は、Register<T> を実体化する REGISTER_COMPONENT より前に定義する
// =====================================================================
namespace ComponentMetaJson
{
    template <>
    struct ComponentSerializer<MaterialComponent>
    {
        static void Save(json &j, const MaterialComponent &c, CCL::Serialization::SaveContext *)
        {
            // スロット数は overrideMaterials 基準。パスは並走列から取る（なければ空）
            json paths = json::array();
            for (size_t i = 0; i < c.overrideMaterials.size(); ++i)
            {
                const char *p = (i < c.overridePaths.size()) ? c.overridePaths[i].c_str() : "";
                paths.push_back(p ? p : "");
            }
            j["paths"] = paths;
            j["customParams"] =
                json::array({c.customParams.x, c.customParams.y, c.customParams.z, c.customParams.w});
        }

        static void Load(const json &j, MaterialComponent &c, CCL::Serialization::LoadContext *)
        {
            // 同じコンポーネントへ読み直すことがあるので、古いハンドルを先に解放する
            // 解放しないとマテリアルプールの枠が漏れる（「既定に戻す」ボタンと同じ後始末）
            for (auto handle : c.overrideMaterials)
                ResourceManager::Instance().UnloadMaterial(handle);

            c.overrideMaterials.clear();
            c.overridePaths.clear();

            if (j.contains("paths") && j["paths"].is_array())
            {
                const auto &paths = j["paths"];
                c.ResizeSlots(paths.size());
                for (size_t i = 0; i < paths.size(); ++i)
                {
                    std::string path   = paths[i].is_string() ? paths[i].get<std::string>() : std::string();
                    c.overridePaths[i] = path;
                    if (!path.empty())
                    {
                        // パスが指す .mat.json を解決（内部でテクスチャも遅延ロード）
                        MaterialHandle h       = ResourceManager::Instance().LoadMaterialAsset(path);
                        c.overrideMaterials[i] = h; // 失敗時は無効ハンドル＝共有マテリアルへフォールバック
                    }
                }
            }

            if (j.contains("customParams") && j["customParams"].is_array() && j["customParams"].size() >= 4)
            {
                const auto &a  = j["customParams"];
                c.customParams = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
            }
        }
    };
}

template <>
struct ComponentMeta<MaterialComponent>
{
    static constexpr const char *displayName       = "Material";
    static constexpr const char *category          = CCL::Categories::Rendering;
    static constexpr int         sortPriority      = 20; // Model の直後に並べる
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    // 登録済みシェーダー名の一覧と、currentHash に当たる添字を集める
    struct ShaderList
    {
        std::vector<const char *> names;
        int                       current = -1; // 見つからなければ -1
    };
    static ShaderList GatherShaders(uint32_t currentHash)
    {
        ShaderList  out;
        const auto &registered = ShaderRegistry::Instance().GetRegisteredNames();
        out.names.reserve(registered.size());
        for (size_t s = 0; s < registered.size(); ++s)
        {
            out.names.push_back(registered[s].first.c_str());
            if (registered[s].second == currentHash) out.current = (int)s;
        }
        return out;
    }

    // 描画時に Shader が自動で書く変数か。マテリアルの値は上書きされるので、編集欄に出さない
    //   useAlbedoMap など: テクスチャの有なから決まる
    //   alphaCutoff      : alphaMode が Mask のときだけ送る（スロットの「アルファ」欄で編集する）
    static bool IsAutoParam(const std::string &name)
    {
        if (name == "alphaCutoff") return true;
        return name.size() > 6 && name.compare(0, 3, "use") == 0 && name.compare(name.size() - 3, 3, "Map") == 0;
    }

    static bool CustomGui(MaterialComponent &compRef, unsigned long long entityID = 0, void *worldPtr = nullptr)
    {
        bool               changed = false;
        MaterialComponent *comp    = &compRef;
        auto              *world   = static_cast<CCL::ECS::World *>(worldPtr);
        auto              &rm      = ResourceManager::Instance();

        // ----- customParams（シェーダー instanceData に直結する共通4値）-----
        if (ImGui::DragFloat4("customParams", &comp->customParams.x, 0.01f))
            changed = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("シェーダーの instanceData.customParams に直結する共通4値。\n"
                              "用途はシェーダー依存（点滅強度・UVスクロール等）。");
        ImGui::Separator();

        // ----- 上書き未使用（共有マテリアル）状態 -----
        if (comp->overrideMaterials.empty())
        {
            ImGui::TextDisabled("共有マテリアルを使用中(既定)");
            bool hasModel = false;
            if (world)
            {
                auto *modelComp = world->GetComponent<ModelComponent>(static_cast<CCL::ECS::EntityID>(entityID));
                if (modelComp && modelComp->GetModel())
                {
                    hasModel = true;
                    if (ImGui::Button("全マテリアルを上書き(編集)"))
                    {
                        const auto &meshes = modelComp->GetModel()->GetMeshes();
                        comp->ResizeSlots(meshes.size());
                        for (size_t i = 0; i < meshes.size(); ++i)
                        {
                            auto          *originalMat = meshes[i].material;
                            MaterialHandle newHandle   = rm.CreateMaterial();
                            MaterialData  *newData     = rm.GetMaterial(newHandle);
                            if (originalMat && originalMat->data && newData)
                            {
                                *newData = *originalMat->data;
                            }
                            comp->SetData(i, newHandle);
                        }
                        changed = true;
                    }
                }
            }
            if (!hasModel && ImGui::Button("空のマテリアルリストを作成"))
            {
                MaterialHandle emptyHandle = rm.CreateMaterial();
                comp->ResizeSlots(1);
                comp->SetData(0, emptyHandle);
                changed = true;
            }
            return changed;
        }

        if (ImGui::Button("既定に戻す(変更を破棄)"))
        {
            for (auto handle : comp->overrideMaterials)
                rm.UnloadMaterial(handle);
            comp->overrideMaterials.clear();
            comp->overridePaths.clear();
            return true;
        }

        // ----- シェーダー（モデル全体で1つ）-----
        //   ModelRenderSystem は先頭のスロットのシェーダーでモデル全体を描くので、選ぶ欄は1つだけにし、
        //   選んだら全スロットに書く（スロットごとに違うシェーダーにはできない）
        ImGui::Separator();
        {
            const MaterialData *firstMat = (comp->overrideMaterials[0].IsValid())
                                               ? rm.GetMaterial(comp->overrideMaterials[0])
                                               : nullptr;
            ShaderList sl  = GatherShaders(firstMat ? firstMat->shaderHash : 0);
            int        cur = sl.current < 0 ? 0 : sl.current;
            if (sl.names.empty())
            {
                ImGui::TextDisabled("シェーダー未登録");
            }
            else if (ImGui::Combo("シェーダー", &cur, sl.names.data(), (int)sl.names.size()))
            {
                const uint32_t targetHash = ShaderRegistry::Instance().GetRegisteredNames()[cur].second;
                for (auto handle : comp->overrideMaterials)
                {
                    if (!handle.IsValid()) continue;
                    if (MaterialData *m = rm.GetMaterial(handle)) m->shaderHash = targetHash;
                }
                changed = true;
            }
        }

        // ----- スロット一覧 -----
        //   スロットの番号はメッシュの番号。見出しには、そのメッシュが本来使うマテリアルの名前を出す
        ImGui::Separator();
        ImGui::Text("マテリアルスロット:");

        const std::vector<Model::Mesh> *meshes = nullptr;
        if (world)
        {
            auto *modelComp = world->GetComponent<ModelComponent>(static_cast<CCL::ECS::EntityID>(entityID));
            if (modelComp && modelComp->GetModel()) meshes = &modelComp->GetModel()->GetMeshes();
        }

        for (size_t i = 0; i < comp->overrideMaterials.size(); ++i)
        {
            MaterialHandle matHandle = comp->overrideMaterials[i];
            if (!matHandle.IsValid()) continue;
            MaterialData *matPtr = rm.GetMaterial(matHandle);
            if (!matPtr) continue;

            ImGui::PushID((int)i);

            const Model::Material *srcMat  = (meshes && i < meshes->size()) ? (*meshes)[i].material : nullptr;
            const char            *matName = (srcMat && !srcMat->name.empty()) ? srcMat->name.c_str() : "(名前なし)";

            char nodeName[160];
            sprintf_s(nodeName, "%s [%zu]", matName, i);

            if (ImGui::TreeNode(nodeName))
            {
                // --- アセット参照状態（このスロットがどの .mat.json 由来か）---
                const std::string &srcPath = (i < comp->overridePaths.size()) ? comp->overridePaths[i] : std::string();
                if (!srcPath.empty())
                    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "アセット: %s", srcPath.c_str());
                else
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "アセット未保存（保存しないとシーン再読込で失われます）");

                if (ImGui::Button(".mat.jsonで保存"))
                {
                    if (auto path = FileDialog::SavePreset("Material", "Assets/Materials", "mat.json", Graphics::Instance().GetWindowHandle()))
                    {
                        // ダイアログが返すのは絶対パス。この文字列はそのままシーン/プレハブの
                        // JSON に保存されるので、相対パスに直してから持つ。絶対パスのままだと
                        // 別の PC で読み込みに失敗し、エラーも出ずに共有マテリアルで描かれる
                        const std::string rel = FileDialog::ConvertToRelativePath(*path);
                        if (rm.SaveMaterialAsset(matHandle, rel))
                        {
                            comp->SetData(i, matHandle, rel);
                            changed = true;
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(".mat.jsonを読込"))
                {
                    if (auto path = FileDialog::OpenPreset("Material", "マテリアルを選択", Graphics::Instance().GetWindowHandle()))
                    {
                        // 保存用と読み込み用で同じ文字列を使う。片方だけ絶対パスにすると、
                        // 同じ .mat.json が絶対・相対の2つのキーでキャッシュに載る
                        const std::string rel       = FileDialog::ConvertToRelativePath(*path);
                        MaterialHandle    newHandle = rm.LoadMaterialAsset(rel);
                        if (newHandle.IsValid())
                        {
                            comp->SetData(i, newHandle, rel);
                            changed = true;
                        }
                    }
                }
                ImGui::Separator();

                // --- アルファの扱い（不透明 / くり抜き / 半透明。描くパスと discard が決まる）---
                {
                    const char *modeNames[] = {"不透明", "くり抜き(Mask)", "半透明(Blend)"};
                    int         mode        = static_cast<int>(matPtr->alphaMode);
                    if (ImGui::Combo("アルファ", &mode, modeNames, IM_ARRAYSIZE(modeNames)))
                    {
                        matPtr->alphaMode = static_cast<AlphaMode>(mode);
                        changed           = true;
                    }
                    if (matPtr->alphaMode == AlphaMode::Mask &&
                        ImGui::SliderFloat("しきい値", &matPtr->alphaCutoff, 0.0f, 1.0f))
                    {
                        changed = true;
                    }
                }

                // --- テクスチャ ---
                if (ImGui::TreeNode("テクスチャ"))
                {
                    const char *slotNames[] = {"AlbedoMap", "NormalMap", "EmissiveMap"};

                    for (const char *slotName : slotNames)
                    {
                        uint32_t      slotHash = CCL::Utils::HashString(slotName);
                        TextureHandle texHandle;
                        bool          hasTexture = matPtr->GetTexture(slotHash, texHandle) && texHandle.IsValid();

                        ImGui::PushID(slotName);
                        ImGui::Text("%s: %s", slotName, hasTexture ? "[Set]" : "[Empty]");
                        ImGui::SameLine();

                        if (ImGui::Button("読込..."))
                        {
                            if (auto path = FileDialog::OpenPreset("Texture", "Select Texture", Graphics::Instance().GetWindowHandle()))
                            {
                                // この文字列は .mat.json の assetPath として保存され、次の読み込みで
                                // LoadTexture に渡る。相対パスにする理由は .mat.json の保存と同じ
                                const std::string s = FileDialog::ConvertToRelativePath(*path);
                                matPtr->SetTexture(slotHash, rm.LoadTexture(s.c_str()), s);
                                changed = true;
                            }
                        }
                        if (!hasTexture && std::string(slotName) == "AlbedoMap")
                        {
                            ImGui::SameLine();
                            if (ImGui::Button("白を設定"))
                            {
                                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> whiteTex;
                                GpuResourceUtils::CreateDummyTexture(Graphics::Instance().GetDevice(), 0xFFFFFFFF, whiteTex.GetAddressOf());
                                matPtr->SetTexture(slotHash, rm.RegisterTexture(whiteTex));
                                changed = true;
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }

                // --- 詳細パラメータ（シェーダーのリフレクションから一覧を作る）---
                //   出すのはマテリアルの定数バッファ（b0〜b7）の色（float4）と数値（float）だけ
                //   b8 以降はカメラ・ライト・影・フォグなどフレームで共通の値で、マテリアルでは変えられない
                //   マテリアルに値がない変数は GPU に 0 が送られるので、0 と表示し、触ったらマテリアルに書く
                if (ImGui::TreeNode("詳細パラメータ"))
                {
                    Shader *shader = ShaderRegistry::Instance().GetShader(matPtr->shaderHash, Graphics::Instance().GetDevice());
                    if (shader)
                    {
                        for (const auto &cbInfo : shader->GetReflection().GetConstantBuffers())
                        {
                            if (cbInfo.bindPoint >= SLOT_CB_SCENE) continue;

                            for (const auto &varInfo : cbInfo.variables)
                            {
                                if (varInfo.type != D3D_SVT_FLOAT) continue;
                                if (IsAutoParam(varInfo.name)) continue;

                                if (varInfo.rows == 1 && varInfo.columns == 4)
                                {
                                    DirectX::XMFLOAT4 val = {0.0f, 0.0f, 0.0f, 0.0f};
                                    matPtr->GetColor(varInfo.nameHash, val);
                                    // HDR: 1.0 を超える値（発光を強めるなど）を入れられる。Float: 0〜255 でなく小数で見せる
                                    if (ImGui::ColorEdit4(varInfo.name.c_str(), &val.x,
                                                          ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
                                    {
                                        matPtr->SetColor(varInfo.nameHash, val);
                                        changed = true;
                                    }
                                }
                                else if (varInfo.rows == 1 && varInfo.columns == 1)
                                {
                                    float val = 0.0f;
                                    matPtr->GetScalar(varInfo.nameHash, val);
                                    if (ImGui::DragFloat(varInfo.name.c_str(), &val, 0.01f))
                                    {
                                        matPtr->SetScalar(varInfo.nameHash, val);
                                        changed = true;
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("シェーダー未読込。");
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        return changed;
    }
#else
    static bool CustomGui(MaterialComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(MaterialComponent, "Material")
REGISTER_COMPONENT_EDITOR_GUI(MaterialComponent)
