#include "ComponentGuiRegistry.h"
#include "ECS/Core/CCL_World.h" // Worldへのアクセスに必要
#include "Game/Core/AllComponents.h"
#include <imgui.h>

#include "Engine/Graphics/Core/GpuResourceUtils.h" // テクスチャロード用
#include "Engine/Graphics/Core/Graphics.h"         // デバイス取得用
#include "Engine/Platform/Dialog.h"              // ファイル選択ダイアログ用

#include "Game/Logics/Combat/Weapon/Attachments/AttachmentMasterData.h" 

#include "Engine/Serialization/Meta/ComponentMetaImGui.h"

// JSONとファイル入出力
#include <fstream>
#include <json.hpp>
#include <filesystem>
using json = nlohmann::json;


// -----------------------------------------------------------------------
// JSON変換ヘルパー
// -----------------------------------------------------------------------

// Config -> JSON
json ConfigToJson(const ParticleSystemConfig &cfg, const char *texPath, const char *rampPath)
{
    // ★安全対策ヘルパー (数値が壊れていたら 0.0f にする)
    auto SafeFloat = [](float val) { return std::isfinite(val) ? val : 0.0f; };

    try {
        std::string safeTexPath  = (texPath && texPath[0] != '\0') ? texPath : "";
        std::string safeRampPath = (rampPath && rampPath[0] != '\0') ? rampPath : "";

        return {
            // 基本設定
            {"max_particle_count", cfg.max_particle_count},
            {"texture_path", safeTexPath},
            {"color_ramp_path", safeRampPath},
            // カラーモード (0:単色, 1:ランプ)
            {"color_mode", cfg.color_mode},
            {"type", cfg.type},
            {"emission_mode", cfg.emission_mode},
            {"burst_count", cfg.burst_count},

            // 発生パラメータ (SafeFloatを通すとより安全です)
            {"spawn_delay_min", SafeFloat(cfg.spawn_delay.x)},
            {"spawn_delay_max", SafeFloat(cfg.spawn_delay.y)},
            {"lifespan_min", SafeFloat(cfg.lifespan.x)},
            {"lifespan_max", SafeFloat(cfg.lifespan.y)},
            {"speed_min", SafeFloat(cfg.emission_speed.x)},
            {"speed_max", SafeFloat(cfg.emission_speed.y)},
            {"gravity", SafeFloat(cfg.gravity)},

            // 範囲・角度
            {"radius_min", SafeFloat(cfg.emission_offset.x)},
            {"radius_max", SafeFloat(cfg.emission_offset.y)},
            {"angle_min", SafeFloat(cfg.emission_cone_angle.x)},
            {"angle_max", SafeFloat(cfg.emission_cone_angle.y)},

            // 見た目
            {"color",
                {SafeFloat(cfg.manual_color.x),
                    SafeFloat(cfg.manual_color.y),
                    SafeFloat(cfg.manual_color.z),
                    SafeFloat(cfg.manual_color.w)}},
            {"size_start", SafeFloat(cfg.emission_size.x)},
            {"size_end", SafeFloat(cfg.emission_size.y)},
            {"fade_in", SafeFloat(cfg.fade_duration.x)},
            {"fade_out", SafeFloat(cfg.fade_duration.y)},

            // 拡張機能
            {"noise_scale", SafeFloat(cfg.noise_scale)},
            {"noise_strength", SafeFloat(cfg.noise_strength)},
            {"uv_scroll_x", SafeFloat(cfg.uv_scroll_speed.x)},
            {"uv_scroll_y", SafeFloat(cfg.uv_scroll_speed.y)},
            {"velocity_stretch", SafeFloat(cfg.velocity_stretch)},
            {"anim_mode", cfg.sprite_anim_mode},
            {"grid_x", cfg.sprite_sheet_grid.x},
            {"grid_y", cfg.sprite_sheet_grid.y},
        };
    }
    catch (std::exception &e) {
        // エラー時はログを出して空のJSONを返す（クラッシュ回避）
        return json({});
    }
}

// JSON -> Config
void JsonToConfig(const json &j, GPUParticleComponent *p)
{
    auto &cfg = p->config;

    // 値があれば読み込む (なければデフォルト維持)
    if (j.contains("max_particle_count")) {
        int newMax = j["max_particle_count"];
        // 最大数が変わる場合はリサイズ処理を通す
        if (newMax != cfg.max_particle_count) {
            p->maxParticles        = newMax;
            cfg.max_particle_count = newMax;
            p->isInitialized       = false; // フラグを折る
        }
    }

    // テクスチャ
    if (j.contains("texture_path")) {
        std::string path = j["texture_path"];
        if (!path.empty()) {
            strcpy_s(p->texturePath, sizeof(p->texturePath), path.c_str());
            p->isInitialized = false; // フラグを折る
        }
    }

    // Color Ramp パスの読み込み
    if (j.contains("color_ramp_path")) {
        std::string path = j["color_ramp_path"];
        if (!path.empty()) {
            strcpy_s(p->colorRampPath, sizeof(p->colorRampPath), path.c_str());
            p->isInitialized = false; // フラグを折る
        }
    }

    if (j.contains("color_mode")) cfg.color_mode = j["color_mode"];

    if (j.contains("type")) cfg.type = j["type"];
    if (j.contains("emission_mode")) cfg.emission_mode = j["emission_mode"];
    if (j.contains("burst_count")) cfg.burst_count = j["burst_count"];

    if (j.contains("spawn_delay_min")) cfg.spawn_delay.x = j["spawn_delay_min"];
    if (j.contains("spawn_delay_max")) cfg.spawn_delay.y = j["spawn_delay_max"];
    if (j.contains("lifespan_min")) cfg.lifespan.x = j["lifespan_min"];
    if (j.contains("lifespan_max")) cfg.lifespan.y = j["lifespan_max"];
    if (j.contains("speed_min")) cfg.emission_speed.x = j["speed_min"];
    if (j.contains("speed_max")) cfg.emission_speed.y = j["speed_max"];
    if (j.contains("gravity")) cfg.gravity = j["gravity"];

    if (j.contains("radius_min")) cfg.emission_offset.x = j["radius_min"];
    if (j.contains("radius_max")) cfg.emission_offset.y = j["radius_max"];
    if (j.contains("angle_min")) cfg.emission_cone_angle.x = j["angle_min"];
    if (j.contains("angle_max")) cfg.emission_cone_angle.y = j["angle_max"];

    if (j.contains("color")) {
        auto c           = j["color"];
        cfg.manual_color = {c[0], c[1], c[2], c[3]};
    }
    if (j.contains("size_start")) cfg.emission_size.x = j["size_start"];
    if (j.contains("size_end")) cfg.emission_size.y = j["size_end"];
    if (j.contains("fade_in")) cfg.fade_duration.x = j["fade_in"];
    if (j.contains("fade_out")) cfg.fade_duration.y = j["fade_out"];

    if (j.contains("noise_scale")) cfg.noise_scale = j["noise_scale"];

    // 拡張機能
     if (j.contains("noise_strength")) cfg.noise_strength = j["noise_strength"];
     if (j.contains("uv_scroll_x")) cfg.uv_scroll_speed.x = j["uv_scroll_x"];
     if (j.contains("uv_scroll_y")) cfg.uv_scroll_speed.y = j["uv_scroll_y"];
     if (j.contains("velocity_stretch")) cfg.velocity_stretch = j["velocity_stretch"];
     if (j.contains("anim_mode")) cfg.sprite_anim_mode = j["anim_mode"];
     if (j.contains("grid_x")) cfg.sprite_sheet_grid.x = j["grid_x"];
     if (j.contains("grid_y")) cfg.sprite_sheet_grid.y = j["grid_y"];
}


// 初期化時にすべてのコンポーネントを名前付きで登録
void ComponentGuiRegistry::Initialize()
{

    //// --- NameComponent ---
    //Register<NameComponent>("Name", [](void *component, EntityID, CCL::ECS::Core::World *) {
    //    auto *c = static_cast<NameComponent *>(component);

    //    // ImGui::InputText は char* を直接受け取れるので、
    //    // std::string のようにバッファ変換がいらず、非常にシンプルで高速です。
    //    ImGui::InputText("Name", c->name, sizeof(c->name));

    //    // ツールチップ（長さ制限の表示）
    //    if (ImGui::IsItemHovered()) {
    //        ImGui::SetTooltip("Max 31 characters");
    //    }
    //});

    // --- Transform ---
    Register<TransformComponent>(
        "Transform", [](void *component, EntityID selfID, CCL::ECS::Core::World *world) {
            auto *t = static_cast<TransformComponent *>(component);

            // 位置・回転・スケール
            ImGui::DragFloat3("Position", &t->position.x, 0.1f);

            // ------------------------------------------------------------------
            // ★修正: 回転を Euler角 (Degree) で編集できるようにする
            // ------------------------------------------------------------------
            // 1. クォータニオン -> オイラー角(Radian) -> Degree 変換
            DirectX::XMFLOAT4 q = t->rotation;

            // 下記は簡易的な変換計算です (Y-Up, Z-Forward 系)
            // 必要に応じて mathライブラリの関数を使ってください
            float sqw = q.w * q.w;
            float sqx = q.x * q.x;
            float sqy = q.y * q.y;
            float sqz = q.z * q.z;
            float unit =
                sqx + sqy + sqz + sqw; // if normalised is one, otherwise is correction factor
            float test = q.x * q.w - q.y * q.z;

            DirectX::XMFLOAT3 euler; // Pitch, Yaw, Roll

            if (test > 0.4995f * unit) { // singularity at north pole
                euler.y = 2.0f * atan2f(q.y, q.x);
                euler.x = DirectX::XM_PIDIV2;
                euler.z = 0;
            }
            else if (test < -0.4995f * unit) { // singularity at south pole
                euler.y = -2.0f * atan2f(q.y, q.x);
                euler.x = -DirectX::XM_PIDIV2;
                euler.z = 0;
            }
            else {
                euler.y = atan2f(2.0f * (q.w * q.y + q.z * q.x), 1.0f - 2.0f * (sqx + sqy));

                // ★修正: 計算結果を変数に受ける
                float sinp = 2.0f * (q.w * q.x - q.y * q.z);

                // ★修正: 誤差対策 (-1.0 ～ 1.0 の範囲に収める)
                if (sinp > 1.0f) sinp = 1.0f;
                if (sinp < -1.0f) sinp = -1.0f;

                euler.x = asinf(sinp); // これで nan にならない

                euler.z = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (sqz + sqx));
            }

            // Radian -> Degree
            DirectX::XMFLOAT3 eulerDeg;
            eulerDeg.x = DirectX::XMConvertToDegrees(euler.x);
            eulerDeg.y = DirectX::XMConvertToDegrees(euler.y);
            eulerDeg.z = DirectX::XMConvertToDegrees(euler.z);

            // 2. ImGuiで編集
            if (ImGui::DragFloat3("Rotation", &eulerDeg.x, 0.1f)) {
                // 3. Degree -> Radian -> Quaternion 変換して書き戻す
                float p = DirectX::XMConvertToRadians(eulerDeg.x); // Pitch (X)
                float y = DirectX::XMConvertToRadians(eulerDeg.y); // Yaw (Y)
                float r = DirectX::XMConvertToRadians(eulerDeg.z); // Roll (Z)

                using namespace DirectX;
                XMVECTOR Q = XMQuaternionRotationRollPitchYaw(p, y, r);
                XMStoreFloat4(&t->rotation, Q);
            }
            // ------------------------------------------------------------------

            ImGui::DragFloat3("Scale", &t->scale.x, 0.01f);

            ImGui::Separator();

            // --- 親子関係の設定 ---
            ImGui::Text("Parenting");

            // ★修正点1: EntityIDは64bitなので int ではなく unsigned long long で扱う
            // 入力欄も InputScalar を使用する
            // (手打ちはミスしやすいのでReadOnly推奨だが、編集するならこうする)
            EntityID currentParentID = t->parentID;

            // "Parent ID" を表示 (64bit対応入力)
            // 手入力で正しいID(4294977302など)を入れるのは困難なので、基本はD&D推奨
            if (ImGui::InputScalar("Parent ID", ImGuiDataType_U64, &currentParentID)) {
                // 自分自身を親にしない
                if (currentParentID != selfID) {
                    t->parentID = currentParentID;
                }
            }

            // 「親から外す」ボタン
            if (t->parentID != 0) {
                ImGui::SameLine();
                if (ImGui::Button("Detach")) {
                    t->parentID = 0;
                }

                // 現在設定されている親IDを10進数で表示（デバッグ確認用）
                ImGui::Text("Current Parent: %llu", t->parentID);
            }
            else {
                ImGui::Text("Current Parent: None");
            }

            // ★修正点2: ドラッグ＆ドロップの受け入れ
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload =
                        ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {

                    // ここは元々ポインタ渡しなので64bit値が正しく取れるはず
                    EntityID droppedID = *(const EntityID *)payload->Data;

                    // 自分自身や、自分の子供を親にしないチェックを入れるとより安全
                    if (droppedID != selfID) {
                        t->parentID = droppedID;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Drag an entity here from Hierarchy to set as Parent.");
            }
        });

    // --- Player ---
    Register<PlayerComponent>("Player", [](void *data, EntityID, CCL::ECS::Core::World *) {
        auto *p = static_cast<PlayerComponent *>(data);
        ImGui::SeparatorText("Player Settings");
        ImGui::DragFloat("Move Speed", &p->moveSpeed, 0.5f, 0.0f, 100.0f);
        ImGui::DragFloat("Turn Speed", &p->turnSpeed, 0.1f, 0.0f, 50.0f);
        ImGui::InputInt("Pad Slot", &p->padSlot);

        if (ImGui::TreeNode("Live Input State")) {
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Move Dir", &p->input.moveDir.x);
            ImGui::Checkbox("Jump", &p->input.jump);
            ImGui::Checkbox("Attack", &p->input.attack);
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    });

    // --- Sphere Collider ---
    Register<SphereColliderComponent>(
        "Sphere Collider", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *s = static_cast<SphereColliderComponent *>(data);
            ImGui::DragFloat("Radius", &s->radius, 0.1f, 0.0f, 100.0f);
            ImGui::BeginDisabled();
            ImGui::Checkbox("Is Colliding", &s->isColliding);
            ImGui::EndDisabled();
        });

    // --- Box Collider (追加) ---
    Register<BoxColliderComponent>(
        "Box Collider", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *b = static_cast<BoxColliderComponent *>(data);
            ImGui::DragFloat3("Center", &b->center.x, 0.1f);
            ImGui::DragFloat3("Size", &b->size.x, 0.1f, 0.0f, 1000.0f);

            ImGui::BeginDisabled();
            ImGui::Checkbox("Is Colliding", &b->isColliding);
            ImGui::EndDisabled();
        });

    // --- Capsule Collider (追加) ---
    Register<CapsuleColliderComponent>(
        "Capsule Collider", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *c = static_cast<CapsuleColliderComponent *>(data);
            ImGui::DragFloat3("Center", &c->center.x, 0.1f);
            ImGui::DragFloat("Radius", &c->radius, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Height", &c->height, 0.1f, 0.0f, 100.0f);

            ImGui::BeginDisabled();
            ImGui::Checkbox("Is Colliding", &c->isColliding);
            ImGui::EndDisabled();
        });

    // --- Model / Static Mesh ---
    Register<ModelComponent>("Model", [](void *data, EntityID, CCL::ECS::Core::World *) {
        auto *m = static_cast<ModelComponent *>(data);
        ImGui::Text("Resource: %s", m->modelPtr ? "Loaded" : "Empty");
        if (m->modelPtr) {
            ImGui::TextDisabled("Path: %s", m->assetPath.c_str());
        }
    });

    Register<StaticMeshComponent>("Static Mesh", [](void *data, EntityID, CCL::ECS::Core::World *) {
        auto *s = static_cast<StaticMeshComponent *>(data);
        ImGui::Checkbox("Large Mesh Opt", &s->isLargeMesh);
    });



    Register<FogComponent>(u8"Fog Settings (フォグ設定)",
        [](void *data, EntityID entity, CCL::ECS::Core::World *world) {
            auto *f = static_cast<FogComponent *>(data);

            ImGui::Checkbox(u8"Enabled (有効)", &f->enabled);
            ImGui::ColorEdit4(u8"Color", &f->color.x);

            if (ImGui::TreeNode(u8"Distance & Height (距離・高さ)")) {
                ImGui::DragFloat(u8"Start Dist", &f->start, 0.5f);
                ImGui::DragFloat(u8"End Dist", &f->end, 0.5f);
                ImGui::DragFloat(u8"Height Level (地面高さ)", &f->heightStart, 0.1f);
                ImGui::DragFloat(u8"Height Density (濃さ)", &f->heightDensity, 0.01f);

                ImGui::DragFloat3(u8"Center (Player)", &f->center.x, 0.1f);
                if (world && ImGui::Button(u8"Snap to Entity")) {
                    auto *t = world->GetComponent<TransformComponent>(entity);
                    if (t) f->center = {t->position.x, t->position.y, t->position.z, 1.0f};
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode(u8"Noise & Animation (揺らぎ)")) {
                // テクスチャ設定
                ImGui::Text(u8"Noise Texture:");
                ImGui::SameLine();
                if (ImGui::Button(u8"Load...")) {
                    char filename[256] = {};
                    if (Dialog::OpenFileName(filename,
                            256,
                            "Image\0*.png;*.jpg;*.dds\0",
                            "Load Noise",
                            Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {
                        strcpy_s(f->noiseTexturePath, filename);
                        GpuResourceUtils::LoadTexture(Graphics::Instance().GetDevice(),
                            filename,
                            f->noiseTextureSRV.ReleaseAndGetAddressOf());
                    }

                    
                }

                // プレビュー
                if (f->noiseTextureSRV) {
                    ImGui::Image((void *)f->noiseTextureSRV.Get(), ImVec2(64, 64));
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        f->noiseTextureSRV.Reset();
                        f->noiseTexturePath[0] = '\0';
                    }
                }

                ImGui::DragFloat(u8"Strength (適用強度)", &f->noiseStrength, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat(u8"Scale (UVサイズ)", &f->noiseScale, 0.001f);
                ImGui::DragFloat2(u8"Scroll Speed (流速)", &f->noiseSpeed.x, 0.01f);


 
                ImGui::TreePop();
            }

            // リムライト
            if (ImGui::TreeNode(u8"Rim Light (Atmosphere)")) {
                ImGui::ColorEdit3(u8"Rim Color", &f->rimColor.x);
                ImGui::DragFloat(u8"Strength (強度)", &f->rimStrength, 0.1f, 0.0f, 10.0f);
                ImGui::DragFloat(u8"Power (鋭さ)", &f->rimPower, 0.1f, 0.1f, 20.0f);

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(u8"値が大きいほど輪郭ギリギリだけが光ります。\n小さいと全"
                                      u8"体がボワーっと光ります。");
                }
                ImGui::TreePop();
            }

        });



   // --- MaterialComponent ---
    Register<MaterialComponent>(
        "Material", [](void *data, EntityID entity, CCL::ECS::Core::World *world) {
            auto *comp = static_cast<MaterialComponent *>(data);

            // -------------------------------------------------------------
            // パターンA: まだオーバーライドしていない (リストが空)
            // -------------------------------------------------------------
            if (comp->overrideMaterials.empty()) {
                ImGui::TextDisabled("Using Shared Materials (Default)");

                bool hasModel = false;
                if (world) {
                    auto *modelComp = world->GetComponent<ModelComponent>(entity);
                    if (modelComp && modelComp->modelPtr) {
                        hasModel = true;

                        if (ImGui::Button("Override All Materials (Edit)")) {
                            const auto &meshes = modelComp->modelPtr->GetMeshes();
                            comp->overrideMaterials.resize(meshes.size());

                            for (size_t i = 0; i < meshes.size(); ++i) {
                                auto *originalMat = meshes[i].material;
                                
                                // ★修正: shared_ptr ではなく管理人に部屋を作ってもらう
                                MaterialHandle newHandle = ResourceManager::Instance().CreateMaterial();
                                MaterialData* newData = ResourceManager::Instance().GetMaterial(newHandle);

                                if (originalMat && originalMat->data && newData) {
                                    // データをコピーして名前を変える
                                    *newData = *originalMat->data;
                                    newData->name += " (Instance)";
                                }
                                
                                // チケットをセットする
                                comp->SetData(i, newHandle);
                            }
                        }
                    }
                }

                // 予備ボタン（モデルが無い場合など）
                if (!hasModel) {
                    if (ImGui::Button("Create Empty Material List")) {
                        // ★修正: 空のチケットを発行してセット
                        MaterialHandle emptyHandle = ResourceManager::Instance().CreateMaterial();
                        comp->overrideMaterials.resize(1, emptyHandle);
                    }
                }
                return;
            }

            // -------------------------------------------------------------
            // パターンB: オーバーライド中 (リスト表示)
            // -------------------------------------------------------------
            if (ImGui::Button("Reset to Default (Discard Changes)")) {
                // リソースの破棄（部屋を空ける）処理を追加
                for(auto handle : comp->overrideMaterials) {
                    ResourceManager::Instance().UnloadMaterial(handle);
                }
                comp->overrideMaterials.clear();
                return;
            }

            ImGui::Separator();
            ImGui::Text("Material Slots:");

            // ★ リスト内の各マテリアルをループして表示
            for (size_t i = 0; i < comp->overrideMaterials.size(); ++i) {
                // 1. チケットを取り出す
                MaterialHandle matHandle = comp->overrideMaterials[i];
                if (!matHandle.IsValid()) continue;

                // 2. 管理人から実体（ポインタ）をもらう
                MaterialData* matPtr = ResourceManager::Instance().GetMaterial(matHandle);
                if (!matPtr) continue;

                // ID被り防止のためインデックスでPush
                ImGui::PushID((int)i);

                // ツリー表示 (名前で折り畳めるように)
                // ★修正: matData-> ではなく matPtr-> を使う
                if (ImGui::TreeNode(matPtr->name.c_str())) {

                    // --- 1. 基本カラー設定 ---
                    if (matPtr->colors.count("materialColor")) {
                        ImGui::ColorEdit4("Main Color", &matPtr->colors["materialColor"].x);
                    }
                    else {
                        if (ImGui::Button("Add Color Property")) {
                            matPtr->colors["materialColor"] = {1, 1, 1, 1};
                        }
                    }

                    // --- 3. シェーダー選択 ---
                    int currentShader = static_cast<int>(matPtr->shaderId);
                    if (ImGui::Combo("Shader",
                            &currentShader,
                            ShaderIdNames,
                            static_cast<int>(ShaderId::EnumCount))) {
                        matPtr->shaderId = static_cast<ShaderId>(currentShader);
                    }

                    // --- 4. テクスチャ設定 ---
                    if (ImGui::TreeNode("Textures")) {
                        const char *slotNames[] = {
                            "DiffuseMap", "NormalMap", "EmissiveMap", "RampTexture"};
                        for (const char *slotName : slotNames) {
                            bool hasTexture =
                                (matPtr->textures.find(slotName) != matPtr->textures.end());

                            ImGui::PushID(slotName);
                            ImGui::Text("%s: %s", slotName, hasTexture ? "[Set]" : "[Empty]");
                            ImGui::SameLine();

                            if (ImGui::Button("Load...")) {
                                char filename[256] = {};
                                if (Dialog::OpenFileName(filename,
                                            256,
                                            "Image Files\0*.png;*.jpg;*.tga;*.bmp\0",
                                            "Select Texture",
                                            Graphics::Instance().GetWindowHandle()) ==
                                    DialogResult::OK) {
                                    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
                                    if (SUCCEEDED(GpuResourceUtils::LoadTexture(
                                            Graphics::Instance().GetDevice(),
                                            filename,
                                            srv.GetAddressOf()))) {
                                        // ★修正: 生のComPtrではなくチケット化してセット
                                        matPtr->textures[slotName] =
                                            ResourceManager::Instance().RegisterTexture(srv);
                                    }
                                }
                            }

                            if (!hasTexture && std::string(slotName) == "DiffuseMap") {
                                ImGui::SameLine();
                                if (ImGui::Button("Set White")) {
                                    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> whiteTex;
                                    GpuResourceUtils::CreateDummyTexture(
                                        Graphics::Instance().GetDevice(),
                                        0xFFFFFFFF,
                                        whiteTex.GetAddressOf());
                                    // ★修正: チケット化してセット
                                    matPtr->textures[slotName] = ResourceManager::Instance().RegisterTexture(whiteTex);
                                }
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop(); // End Textures
                    }

                    // --- 5. 詳細設定 (Scalars) ---
                    if (ImGui::TreeNode("Advanced Parameters")) {
                        // ★修正: matPtr->scalars を参照する
                        for (auto &[name, val] : matPtr->scalars) {
                            ImGui::DragFloat(name.c_str(), &val, 0.01f);
                        }
                        ImGui::TreePop();
                    }

                    ImGui::TreePop(); // End Material Node
                }
                ImGui::PopID(); // End Loop ID
            }
        });

    // --- Point Light ---
    Register<PointLightComponent>("PointLight", [](void *data, EntityID, CCL::ECS::Core::World *) {
        auto *l = static_cast<PointLightComponent *>(data);

        ImGui::ColorEdit3("Color", &l->color.x);
        ImGui::DragFloat("Intensity", &l->intensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Range", &l->range, 0.1f, 0.1f, 1000.0f);

        ImGui::Separator();
        ImGui::Text("Attenuation");
        ImGui::DragFloat("Constant", &l->constant, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Linear", &l->linear, 0.001f, 0.0f, 1.0f, "%.4f");
        ImGui::DragFloat("Quadratic", &l->quadratic, 0.0001f, 0.0f, 1.0f, "%.5f");
    });

    // --- Directional Light ---
    Register<DirectionalLightComponent>(
        "DirectionalLight", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *l = static_cast<DirectionalLightComponent *>(data);

            if (ImGui::CollapsingHeader("Main Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::ColorEdit3("Sun Color", &l->color.x);
                ImGui::DragFloat("Intensity", &l->intensity, 0.01f, 0.0f, 10.0f);
            }

            if (ImGui::CollapsingHeader("Hemispheric Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::ColorEdit4("Sky Color", &l->skyColor.x);
                ImGui::ColorEdit4("Ground Color", &l->groundColor.x);
            }
        });

    // --- Spot Light ---
    Register<SpotLightComponent>("SpotLight", [](void *data, EntityID, CCL::ECS::Core::World *) {
        auto *l = static_cast<SpotLightComponent *>(data);

        ImGui::ColorEdit3("Color", &l->color.x);
        ImGui::DragFloat("Intensity", &l->intensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Range", &l->range, 0.1f, 0.1f, 1000.0f);

        ImGui::Separator();

        float innerDeg = DirectX::XMConvertToDegrees(acosf(std::clamp(l->innerCos, -1.0f, 1.0f)));
        float outerDeg = DirectX::XMConvertToDegrees(acosf(std::clamp(l->outerCos, -1.0f, 1.0f)));

        bool angleChanged = false;
        if (ImGui::DragFloat("Inner Angle", &innerDeg, 0.5f, 0.0f, 180.0f)) angleChanged = true;
        if (ImGui::DragFloat("Outer Angle", &outerDeg, 0.5f, 0.0f, 180.0f)) angleChanged = true;

        if (angleChanged) {
            l->innerCos = cosf(DirectX::XMConvertToRadians(innerDeg));
            l->outerCos = cosf(DirectX::XMConvertToRadians(outerDeg));
        }
    });

    Register<ShadowMapConfigComponent>(
        "ShadowMapConfig", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *c = static_cast<ShadowMapConfigComponent *>(data);

            ImGui::DragFloat(u8"Ortho Size (範囲)", &c->orthoSize, 0.5f, 1.0f, 200.0f);
            ImGui::DragFloat(u8"Bias (縞模様対策)", &c->shadowBias, 0.0001f, 0.0f, 0.01f, "%.5f");
            ImGui::ColorEdit4(u8"Shadow Color", &c->shadowColor.x);
        });


    // --- Enemy Component ---

    //ComponentMetaImGui::RegisterGuiMeta<EnemyComponent>(*this);


    // --- Health Component ---
    Register<HealthComponent>(
        "Health", [](void *data, EntityID entityID, CCL::ECS::Core::World *world) {
            auto *h = static_cast<HealthComponent *>(data);

            float fraction = (h->maxHealth > 0.0f) ? (h->currentHealth / h->maxHealth) : 0.0f;
            char  hpLabel[32];
            sprintf_s(hpLabel, "%.0f / %.0f", h->currentHealth, h->maxHealth);

            if (fraction > 0.6f)
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            else if (fraction > 0.3f)
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

            ImGui::ProgressBar(fraction, ImVec2(-1, 0), hpLabel);
            ImGui::PopStyleColor();

            ImGui::DragFloat("Max HP", &h->maxHealth, 1.0f, 1.0f, 10000.0f);

            // ★ Tag::DeadTagの有無で「isDead」のチェック状態を作る
            bool isDead = world->HasComponent<Tag::DeadTag>(entityID);

            if (ImGui::SliderFloat("Current HP", &h->currentHealth, 0.0f, h->maxHealth)) {
                if (h->currentHealth <= 0) {
                    if (!isDead) world->AddComponent<Tag::DeadTag>(entityID);
                }
                else {
                    if (isDead) world->RequestRemoveComponent<Tag::DeadTag>(entityID);
                }
            }

            ImGui::Separator();

            // ★ UIから直接生死を切り替えた場合の処理
            if (ImGui::Checkbox("Is Dead", &isDead)) {
                if (isDead) {
                    world->AddComponent<Tag::DeadTag>(entityID);
                    h->currentHealth = 0.0f;
                }
                else {
                    world->RequestRemoveComponent<Tag::DeadTag>(entityID);
                    if (h->currentHealth <= 0.0f)
                        h->currentHealth = h->maxHealth; // 生き返らせた時にHPを回復
                }
            }

            // --- Invincibility Timer UI ---
            if (h->invincibilityDuration > 0.0f) {
                float remaining = h->invincibilityTimer;
                float duration  = h->invincibilityDuration;

                float fraction = remaining / duration;
                fraction       = std::clamp(fraction, 0.0f, 1.0f);

                ImVec4 barColor;
                if (fraction > 0.6f)
                    barColor = ImVec4(0.3f, 0.8f, 1.0f, 1.0f); // 青
                else if (fraction > 0.3f)
                    barColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // 黄
                else
                    barColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // 赤

                ImGui::Text("Invincibility");
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);

                char label[64];
                sprintf_s(label, "%.2f / %.2f sec", remaining, duration);

                ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);

                ImGui::PopStyleColor();
            }
            else {
                ImGui::TextDisabled("Invincibility: Ready");
            }

            ImGui::DragFloat("Invincible Duration", &h->invincibilityDuration, 0.1f);

            const char *teams[]     = {"Player", "Enemy", "Neutral"};
            int         currentTeam = static_cast<int>(h->team);
            if (ImGui::Combo("Team", &currentTeam, teams, IM_ARRAYSIZE(teams))) {
                h->team = static_cast<TeamID>(currentTeam);
            }

            if (ImGui::Button("Kill")) {
                h->currentHealth = 0;
                if (!world->HasComponent<Tag::DeadTag>(entityID)) {
                    world->AddComponent<Tag::DeadTag>(entityID);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Full Heal")) {
                h->currentHealth = h->maxHealth;
                world->RequestRemoveComponent<Tag::DeadTag>(entityID);
            }
        });

    // --- Bloom Config ---
    Register<BloomConfigComponent>(
        "BloomConfig", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *c = static_cast<BloomConfigComponent *>(data);

            ImGui::Checkbox("Enable Bloom", &c->enable);
            if (c->enable) {
                ImGui::DragFloat(u8"Threshold（閾値）", &c->threshold, 0.01f, 0.0f, 10.0f);
                ImGui::DragFloat(u8"Intensity（強度）", &c->intensity, 0.1f, 0.0f, 40.0f);
                ImGui::DragFloat(u8"Soft Knee（滑らかさ）", &c->softKnee, 0.01f, 0.0f, 1.0f);
                ImGui::DragFloat(u8"Radius（広がり）", &c->radius, 0.01f, 0.0f, 5.0f);
            }
        });

    // --- ToneMap Config ---
    Register<ToneMapConfigComponent>(
        "ToneMapConfigComponent", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *t = static_cast<ToneMapConfigComponent *>(data);
            ImGui::DragFloat(u8"exposure（露出）", &t->exposure, 0.05f, 0.0f, 20.0f);
        });

    // ---------------------------------------------------------
    // Primitive Component (形状デバッグ・プロトタイプ用)
    // ---------------------------------------------------------
    Register<PrimitiveComponent>(
        "Primitive Shape", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *p = static_cast<PrimitiveComponent *>(data);

            // 1. 形状タイプの選択
            const char *typeNames[] = {"Box", "Sphere", "Cylinder", "Capsule", "Cone"};
            int         currentType = static_cast<int>(p->type);

            if (ImGui::Combo("Type", &currentType, typeNames, IM_ARRAYSIZE(typeNames))) {
                p->type = static_cast<PrimitiveType>(currentType);
            }

            // 2. 共通設定
            ImGui::ColorEdit4("Color", &p->color.x);
            ImGui::Checkbox("Wireframe", &p->isWireframe);

            ImGui::Separator();

            // 3. タイプごとのパラメータ表示
            switch (p->type) {
            case PrimitiveType::Box:
                ImGui::DragFloat3("Size", &p->size.x, 0.1f, 0.0f, 100.0f);
                break;

            case PrimitiveType::Sphere:
                ImGui::DragFloat("Radius", &p->radius, 0.1f, 0.0f, 100.0f);
                break;

            case PrimitiveType::Cylinder:
                ImGui::DragFloat("Radius", &p->radius, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("Height", &p->height, 0.1f, 0.0f, 100.0f);
                break;

            case PrimitiveType::Capsule:
                ImGui::DragFloat("Radius", &p->radius, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("Height", &p->height, 0.1f, 0.0f, 100.0f);
                break;

            case PrimitiveType::Cone:
                ImGui::DragFloat("Radius", &p->radius, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("Height", &p->height, 0.1f, 0.0f, 100.0f);
                break;
            }
        });


    // ★★★ GPUパーティクル用GUI ★★★
    Register<GPUParticleComponent>(
        "GPU Particle", [](void *component, EntityID, CCL::ECS::Core::World *) {
            auto *p   = static_cast<GPUParticleComponent *>(component);
            auto &cfg = p->config;

            // =========================================================
            // 1. 管理・システム設定 (Management & System)
            // =========================================================

            // --- プリセット保存/読込 ---
            if (ImGui::Button(u8"プリセット保存 (Save Preset)")) {
                char filename[256] = {};
                HWND hWnd          = Graphics::Instance().GetWindowHandle();
                if (Dialog::SaveFileName(filename,
                        256,
                        "JSON Files\0*.json\0",
                        "Save Particle Preset",
                        "json",
                        hWnd) == DialogResult::OK) {
                    std::ofstream o(filename);
                    json          j = ConfigToJson(cfg, p->texturePath, p->colorRampPath);
                    o << std::setw(4) << j << std::endl;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"プリセット読込 (Load Preset)")) {
                char filename[256] = {};
                HWND hWnd          = Graphics::Instance().GetWindowHandle();
                if (Dialog::OpenFileName(
                        filename, 256, "JSON Files\0*.json\0", "Load Particle Preset", hWnd) ==
                    DialogResult::OK) {
                    std::ifstream i(filename);
                    if (i.is_open()) {
                        json j;
                        i >> j;
                        JsonToConfig(j, p);
                        cfg.burst_trigger = 0.0f; // バーストトリガーはリセット

                        // ★追加: 読み込んだデータを使ってGPUリソースを再構築する
                        // これを呼ばないと、画面上の見た目は古いままになります。
                        // ★変更後: 初期化フラグを折るだけでシステムが勝手に再構築する
                        p->maxParticles  = cfg.max_particle_count;
                        p->isInitialized = false;
                    }
                }
            }

            ImGui::Separator();

            // --- 基本システム設定 ---
            ImGui::TextDisabled(u8"システム設定 (System)");

            // アクティブ数表示
            ImGui::Text(u8"稼働数 (Active): %d", cfg.max_particle_count);

            // 最大数変更 (Enter確定)
            int tempMax = cfg.max_particle_count;
            if (ImGui::InputInt(
                    u8"最大許容量 (Max Capacity)", &tempMax, 0, 0)) { // 0,0でボタン非表示
                // 数値を書き換えてフラグを折るだけ
                if (tempMax > 0) {
                    p->maxParticles  = tempMax;
                    p->isInitialized = false;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    u8"Enterキーで適用します。\n注意: パーティクルが全てリセットされます。");
            }

            // 描画タイプ
            const char *typeNames[] = {"Point (Color Only)", "Quad (Texture)"};
            ImGui::Combo(
                u8"描画タイプ (Render Type)", &cfg.type, typeNames, IM_ARRAYSIZE(typeNames));

            ImGui::Spacing();

            // =========================================================
            // 2. 発生設定 (Emission)
            // =========================================================
            if (ImGui::TreeNode(u8"発生設定 (Emission)")) {

                // モード切替
                const char *modes[] = {"Continuous (継続)", "Burst (爆発)"};
                ImGui::Combo(u8"発生モード (Mode)", &cfg.emission_mode, modes, IM_ARRAYSIZE(modes));

                if (cfg.emission_mode == 1) { // Burst
                    ImGui::DragInt(u8"発生数 (Count)", &cfg.burst_count, 1, 0, 10000);
                    if (ImGui::Button(u8"発火テスト (Trigger)", ImVec2(-1, 0))) {
                        p->TriggerBurst();
                    }
                }
                else { // Continuous
                    ImGui::DragFloat2(u8"発生間隔 (Delay Min/Max)",
                        &cfg.spawn_delay.x,
                        0.001f,
                        0.0f,
                        1.0f,
                        "%.4f");
                }

                ImGui::Separator();
                ImGui::TextDisabled(u8"形状と位置 (Shape & Position)");
                ImGui::DragFloat4(u8"位置オフセット (Pos Offset)", &cfg.emission_position.x, 0.1f);
                ImGui::DragFloat2(u8"半径 (Radius Min/Max)", &cfg.emission_offset.x, 0.1f);

                // 【追加】 エミッターの形状変形
                ImGui::DragFloat(
                    u8"形状比率 X (Shape Stretch X)", &cfg.emission_stretch_x, 0.01f, 0.0f, 10.0f);
                ImGui::DragFloat(
                    u8"形状比率 Z (Shape Stretch Z)", &cfg.emission_stretch_z, 0.01f, 0.0f, 10.0f);

                // 【追加】 Y軸の幅（高さ）
                ImGui::DragFloat(
                    u8"高さ範囲 Y (Height Volume)", &cfg.particle_offset_y, 0.1f, 0.0f, 50.0f);

                ImGui::DragFloat2(u8"角度 (Angle Min/Max)", &cfg.emission_cone_angle.x, 0.1f);

                ImGui::TreePop();
            }

            // =========================================================
            // 3. 挙動・物理設定 (Movement & Physics)
            // =========================================================
            if (ImGui::TreeNode(u8"挙動・物理 (Movement)")) {
                ImGui::DragFloat(u8"重力 (Gravity)", &cfg.gravity, 0.1f);
                ImGui::DragFloat2(u8"速度 (Speed Min/Max)", &cfg.emission_speed.x, 0.1f);

                // 【追加】 回転速度
                ImGui::DragFloat2(
                    u8"回転速度 (Rot Speed Min/Max)", &cfg.emission_angular_speed.x, 0.1f);

                ImGui::DragFloat2(u8"寿命 (Life Min/Max)", &cfg.lifespan.x, 0.1f);

                ImGui::Separator();
                ImGui::TextDisabled(u8"乱流・ノイズ (Turbulence)");
                ImGui::DragFloat(u8"ノイズ強度 (Strength)", &cfg.noise_strength, 0.1f);
                ImGui::DragFloat(u8"ノイズサイズ (Scale)", &cfg.noise_scale, 0.01f);

                ImGui::TreePop();
            }

            // =========================================================
            // 4. 見た目・アニメーション (Appearance)
            // =========================================================
            if (ImGui::TreeNode(u8"見た目 (Appearance)")) {

                // 描画モード選択コンボボックス
                const char *modes[]     = {"Additive (Fire/Light)", "Alpha Blend (Smoke/Dark)"};
                int         currentMode = p->config.render_mode;
                if (ImGui::Combo("Render Mode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
                    p->config.render_mode = currentMode;
                }

                ImGui::ColorEdit4(u8"基本色 (Base Color)", &cfg.manual_color.x);

                const char *colorModes[] = {u8"単色 (Single Color)", u8"カラーランプ (Color Ramp)"};
                ImGui::Combo(u8"カラーモード (Color Mode)",
                    &cfg.color_mode,
                    colorModes,
                    IM_ARRAYSIZE(colorModes));

                if (cfg.color_mode == 1) 
                {
                    // カラーランプモードの説明
                    ImGui::TextDisabled(u8"※テクスチャ項目の Color Ramp が使用されます");
                }

                ImGui::DragFloat2(u8"サイズ (Size Start/End)", &cfg.emission_size.x, 0.1f);
                ImGui::DragFloat2(u8"フェード (Fade In/Out)", &cfg.fade_duration.x, 0.01f);

                ImGui::Separator();
                ImGui::TextDisabled(u8"特殊効果 (Effects)");

                // ストレッチ
                ImGui::DragFloat(
                    u8"速度ストレッチ (Stretch)", &cfg.velocity_stretch, 0.01f, 0.0f, 10.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(u8"移動方向に引き伸ばします (0.0=無効)");

                // UVスクロール
                ImGui::DragFloat2(u8"UVスクロール (UV Scroll)", &cfg.uv_scroll_speed.x, 0.01f);

                ImGui::Separator();
                ImGui::TextDisabled(u8"スプライトアニメ (Sprite Sheet)");

                // 分割数
                int grid[2] = {(int)cfg.sprite_sheet_grid.x, (int)cfg.sprite_sheet_grid.y};
                if (ImGui::DragInt2(u8"分割数 (Grid X/Y)", grid, 0.1f, 1, 16)) {
                    cfg.sprite_sheet_grid.x = grid[0];
                    cfg.sprite_sheet_grid.y = grid[1];
                }

                // モード
                const char *animModes[] = {"Random (Fixed)", "Life Cycle (Animate)"};
                ImGui::Combo(u8"再生モード (Anim Mode)",
                    &cfg.sprite_anim_mode,
                    animModes,
                    IM_ARRAYSIZE(animModes));

                ImGui::TreePop();
            }

            // =========================================================
            // 5. テクスチャ設定 (Textures)
            // =========================================================
            if (ImGui::TreeNode(u8"テクスチャ (Textures)")) {

                // --- メインテクスチャ ---
                ImGui::TextDisabled(u8"メインテクスチャ (Main Texture)");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
                ImGui::InputText("##texPath", p->texturePath, sizeof(p->texturePath));
                ImGui::SameLine();

                if (ImGui::Button(u8"読込##Main")) {
                    char        filename[256] = {};
                    HWND        hWnd          = Graphics::Instance().GetWindowHandle();
                    const char *filter =
                        "Image Files\0*.png;*.jpg;*.tga;*.bmp;*.dds\0All Files\0*.*\0";
                    if (Dialog::OpenFileName(
                            filename, sizeof(filename), filter, "Select Texture", hWnd) ==
                        DialogResult::OK) {
                        // 相対パス変換処理...
                        namespace fs                = std::filesystem;
                        fs::path        absPath     = filename;
                        fs::path        currentPath = fs::current_path();
                        std::error_code ec;
                        fs::path        relPath = fs::relative(absPath, currentPath, ec);
                        // パスをコピーしてフラグを折る
                        if (!ec && !relPath.empty()) {
                            strcpy_s(p->texturePath,
                                sizeof(p->texturePath),
                                relPath.generic_string().c_str());
                        }
                        else {
                            strcpy_s(p->texturePath, sizeof(p->texturePath), filename);
                        }
                        p->isInitialized = false; // 再読み込み指示
                    }
                }

                // プレビュー
                // チケットから管理人経由でポインタをもらう
                ID3D11ShaderResourceView *texSRV =
                    ResourceManager::Instance().GetTexture(p->texture);
                if (texSRV) {
                    ImGui::Image((void *)texSRV,
                        ImVec2(64, 64),
                        ImVec2(0, 0),
                        ImVec2(1, 1),
                        ImVec4(1, 1, 1, 1),
                        ImVec4(1, 1, 1, 0.5f));
                    ImGui::SameLine();
                    if (ImGui::Button(u8"クリア##Main")) {
                        memset(p->texturePath, 0, sizeof(p->texturePath));
                        p->texture = TextureHandle{}; // チケットを空に
                    }
                }

                ImGui::Separator();

                // --- カラーランプ ---
                ImGui::TextDisabled(u8"カラーランプ (Color Ramp)");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
                ImGui::InputText("##rampPath", p->colorRampPath, sizeof(p->colorRampPath));
                ImGui::SameLine();

                if (ImGui::Button(u8"読込##Ramp")) {
                    char        filename[256] = {};
                    HWND        hWnd          = Graphics::Instance().GetWindowHandle();
                    const char *filter =
                        "Image Files\0*.png;*.jpg;*.tga;*.bmp;*.dds\0All Files\0*.*\0";
                    if (Dialog::OpenFileName(
                            filename, sizeof(filename), filter, "Select Color Ramp", hWnd) ==
                        DialogResult::OK) {
                        // 相対パス変換処理...
                        namespace fs                = std::filesystem;
                        fs::path        absPath     = filename;
                        fs::path        currentPath = fs::current_path();
                        std::error_code ec;
                        fs::path        relPath = fs::relative(absPath, currentPath, ec);
                        // ★変更後:
                        if (!ec && !relPath.empty()) {
                            strcpy_s(p->colorRampPath,
                                sizeof(p->colorRampPath),
                                relPath.generic_string().c_str());
                        }
                        else {
                            strcpy_s(p->colorRampPath, sizeof(p->colorRampPath), filename);
                        }
                        p->isInitialized = false; // 再読み込み指示
                    }
                }

                // プレビュー
                // 変更前: if (p->colorRampSRV) { ImGui::Image((void *)p->colorRampSRV, ...
                ID3D11ShaderResourceView *rampSRV =
                    ResourceManager::Instance().GetTexture(p->colorRamp);
                if (rampSRV) {
                    ImGui::Image((void *)rampSRV,
                        ImVec2(128, 16),
                        ImVec2(0, 0),
                        ImVec2(1, 1),
                        ImVec4(1, 1, 1, 1),
                        ImVec4(1, 1, 1, 0.5f));
                    ImGui::SameLine();
                    if (ImGui::Button(u8"クリア##Ramp")) {
                        memset(p->colorRampPath, 0, sizeof(p->colorRampPath));
                        p->colorRamp = TextureHandle{};
                    }
                }

                ImGui::TreePop();
            }
        });


 
        // =================================================================================
        // AnimatorComponent GUI
        // =================================================================================
        Register<AnimatorComponent>(
            "Animator", [](void *data, EntityID entity, CCL::ECS::Core::World *world) {
                auto *anim = static_cast<AnimatorComponent *>(data);

                // --- 1. 基本設定 (再生速度・ループ) ---
                ImGui::DragFloat("Speed", &anim->playbackSpeed, 0.05f, -5.0f, 5.0f);
                ImGui::Checkbox("Loop", &anim->isLoop);

                ImGui::Separator();

                // --- 2. アニメーション選択 (ModelComponent連携) ---
                // 同じエンティティが ModelComponent を持っているか確認
                auto *modelComp = world->GetComponent<ModelComponent>(entity);
                bool  animFound = false;

                if (modelComp && modelComp->modelPtr) {
                    auto *resource = modelComp->modelPtr->GetResource();
                    if (resource) {
                        const auto &animations = resource->GetAnimations();
                        if (!animations.empty()) {
                            animFound = true;

                            // 現在のアニメーション名を取得
                            std::string currentName = "(None)";
                            if (anim->currentAnimIndex >= 0 &&
                                anim->currentAnimIndex < (int)animations.size()) {
                                currentName = animations[anim->currentAnimIndex].name;
                            }
                            else if (anim->currentAnimIndex == -1) {
                                currentName = "(Stop)";
                            }

                            // コンボボックス表示
                            if (ImGui::BeginCombo("Animation", currentName.c_str())) {
                                // "Stop" (-1) の選択肢
                                bool isStop = (anim->currentAnimIndex == -1);
                                if (ImGui::Selectable("(Stop)", isStop)) {
                                    anim->Play(-1);
                                }
                                if (isStop) ImGui::SetItemDefaultFocus();

                                // 各アニメーションの選択肢
                                for (int i = 0; i < (int)animations.size(); ++i) {
                                    bool isSelected = (anim->currentAnimIndex == i);
                                    if (ImGui::Selectable(animations[i].name.c_str(), isSelected)) {
                                        // 切り替え (設定済みのループ・速度設定は維持)
                                        anim->Play(i, anim->isLoop, anim->playbackSpeed);
                                    }
                                    if (isSelected) {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            // --- 3. 再生状況 (プログレスバー) ---
                            if (anim->currentAnimIndex >= 0 &&
                                anim->currentAnimIndex < (int)animations.size()) {
                                float duration = animations[anim->currentAnimIndex].secondsLength;
                                if (duration > 0.0f) {
                                    float progress = anim->currentTimer / duration;
                                    // ループ時は 0.0~1.0 に正規化して表示
                                    if (anim->isLoop) progress = fmod(progress, 1.0f);
                                    // 終了時は1.0で止める
                                    else if (progress > 1.0f)
                                        progress = 1.0f;

                                    char overlay[32];
                                    sprintf_s(
                                        overlay, "%.2f / %.2f s", anim->currentTimer, duration);
                                    ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), overlay);
                                }
                            }
                        }
                    }
                }

                if (!animFound) {
                    ImGui::TextDisabled("No Model or Animations found.");
                    // モデルがなくてもインデックスを直接いじれるようにしておく(デバッグ用)
                    ImGui::InputInt("Anim Index (Raw)", &anim->currentAnimIndex);
                }

                // --- 4. デバッグ情報（ブレンディング状態など） ---
                if (ImGui::TreeNode("Debug Info")) {
                    ImGui::Text("Timer: %.3f", anim->currentTimer);
                    ImGui::Text("Finished: %s", anim->isFinished ? "True" : "False");

                    if (anim->targetAnimIndex != -1) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1, 1, 0, 1),
                            "Transitioning: %d -> %d",
                            anim->currentAnimIndex,
                            anim->targetAnimIndex);

                        float t = 0.0f;
                        if (anim->transitionDuration > 0.0f)
                            t = anim->transitionTimer / anim->transitionDuration;

                        ImGui::ProgressBar(t, ImVec2(0.0f, 0.0f), "Blending");
                    }
                    ImGui::TreePop();
                }
            });


        // ---------------------------------------------------------
        // Virtual Camera Components
        // ---------------------------------------------------------

        // ヘルパー: EntityIDリンク用のUI (ターゲット指定用)
        auto DrawEntityLink = [](const char *label, EntityID &targetID, EntityID selfID) {
            EntityID inputID = targetID;
            // 手動入力 (64bit)
            if (ImGui::InputScalar(label, ImGuiDataType_U64, &inputID)) {
                if (inputID != selfID) targetID = inputID;
            }

            // ドラッグ＆ドロップ受け入れ
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload =
                        ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                    EntityID droppedID = *(const EntityID *)payload->Data;
                    if (droppedID != selfID) targetID = droppedID;
                }
                ImGui::EndDragDropTarget();
            }

            // 解除ボタン
            if (targetID != 0) {
                ImGui::SameLine();
                if (ImGui::Button((std::string("Clear##") + label).c_str())) {
                    targetID = 0;
                }
                ImGui::TextDisabled("Linked: %llu", targetID);
            }
            else {
                ImGui::TextDisabled("None");
            }
        };

        // --- Virtual Camera (基本) ---
        Register<VirtualCamera>(
            "Virtual Camera (Base)", [](void *data, EntityID, CCL::ECS::Core::World *) {
                auto *c = static_cast<VirtualCamera *>(data);

                ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "[ Priority System ]");
                ImGui::DragInt("Priority", &c->priority, 1, -100, 100);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("高い数値ほど優先されます。\n例: Player=10, Event=50");

                ImGui::DragFloat("Blend Time", &c->blendTime, 0.1f, 0.0f);

                ImGui::Separator();
                ImGui::Text("Lens Settings");
                ImGui::DragFloat("BlendTime", &c->blendTime, 0.1f,0.0f);
                ImGui::SliderFloat("FOV", &c->fov, 1.0f, 179.0f, "%.1f deg");
                ImGui::DragFloat("Near Clip", &c->nearClip, 0.1f, 0.01f, 100.0f);
                ImGui::DragFloat("Far Clip", &c->farClip, 10.0f, 10.0f, 100000.0f);

                // デバッグ用: 計算結果の表示（読み取り専用）
                if (ImGui::TreeNode("Debug: Computed Result")) {
                    ImGui::BeginDisabled();
                    ImGui::DragFloat3("Pos", &c->resultPos.x);
                    ImGui::DragFloat3("LookAt", &c->resultLookAt.x);
                    ImGui::Checkbox("Valid", &c->isValid);
                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }
            });

        // --- Camera Body (Move) ---
        Register<CameraBodyFollow>("Camera Body (Follow)",
            [DrawEntityLink](void *data, EntityID self, CCL::ECS::Core::World *) {
                auto *c = static_cast<CameraBodyFollow *>(data);

                ImGui::SeparatorText("Target Settings");
                DrawEntityLink("Target Entity", c->target, self);

                ImGui::SeparatorText("Follow Behavior");
                ImGui::DragFloat3("Offset", &c->offset.x, 0.1f);
                ImGui::DragFloat("Damping", &c->damping, 0.1f, 0.0f, 20.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        u8"値が大きいほど素早く追従します。\n小さいとフワッと遅れます。");

                ImGui::Checkbox("Lock Y Axis", &c->lockY);
            });

        // --- Camera Aim (Look) ---
        Register<CameraAimLookAt>("Camera Aim (LookAt)",
            [DrawEntityLink](void *data, EntityID self, CCL::ECS::Core::World *) {
                auto *c = static_cast<CameraAimLookAt *>(data);

                ImGui::SeparatorText("Target Settings");
                DrawEntityLink("Look Target", c->target, self);

                ImGui::SeparatorText("Aim Behavior");
                ImGui::DragFloat3("Offset", &c->offset.x, 0.1f);
                ImGui::DragFloat("Damping", &c->damping, 0.1f, 0.0f, 20.0f, "%.1f");
            });

        // --- Camera Shake ---
        Register<CameraShake>("Camera Shake", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *s = static_cast<CameraShake *>(data);

            ImGui::SeparatorText("Shake Status");

            // 現在の揺れ具合をバーで表示
            float progress = (s->maxDuration > 0.0f) ? (s->duration / s->maxDuration) : 0.0f;
            ImGui::ProgressBar(progress, ImVec2(-1, 0), "Trauma");

            ImGui::DragFloat("Current Time", &s->duration, 0.01f);
            ImGui::DragFloat("Max Duration", &s->maxDuration, 0.1f);
            ImGui::DragFloat("Amplitude", &s->amplitude, 0.1f);

            ImGui::Separator();
            if (ImGui::Button("Test Shake (Weak)")) {
                s->SetShake(0.5f, 0.5f);
            }
            ImGui::SameLine();
            if (ImGui::Button("Test Shake (Strong)")) {
                s->SetShake(2.0f, 1.0f);
            }
        });

        // Register<CameraBodyFollow>(...) の近くに追加すると見つけやすいです
        Register<CameraBodyFree>(
            "CameraBodyFree", [](void *data, EntityID, CCL::ECS::Core::World *) {
                auto *c = static_cast<CameraBodyFree *>(data);

                ImGui::SeparatorText("Settings");
                ImGui::DragFloat("Move Speed", &c->moveSpeed, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("Look Speed", &c->lookSpeed, 0.01f, 0.0f, 5.0f);

                ImGui::SeparatorText("Debug Info");
                // デバッグ用に現在の角度を表示（手動でいじることも可能にする）
                ImGui::DragFloat("Cur Yaw", &c->currentYaw, 1.0f);
                ImGui::DragFloat("Cur Pitch", &c->currentPitch, 1.0f, -89.0f, 89.0f);
            });


        // ---------------------------------------------------------
        // Trigger Component
        // ---------------------------------------------------------
        Register<TriggerComponent>("Trigger", [](void *data, EntityID, CCL::ECS::Core::World *) {
            auto *t = static_cast<TriggerComponent *>(data);

            ImGui::DragFloat(u8"Radius (半径)", &t->radius, 0.1f, 0.0f, 100.0f);
            ImGui::Checkbox(u8"One Shot (一回のみ)", &t->isOneShot);

            ImGui::Separator();

            // デバッグ情報（読み取り専用）
            ImGui::BeginDisabled();
            ImGui::Checkbox(u8"Triggered (発動済)", &t->hasTriggered);
            ImGui::EndDisabled();

            // デバッグ用リセットボタン（テスト時に便利です）
            if (t->hasTriggered) {
                if (ImGui::Button("Reset Trigger State")) {
                    t->hasTriggered = false;
                }
            }
        });

        // ---------------------------------------------------------
        // Lua Script Component
        // ---------------------------------------------------------
        Register<LuaScriptComponent>(
            "Lua Script", [](void *data, EntityID, CCL::ECS::Core::World *) {
                auto *s = static_cast<LuaScriptComponent *>(data);

                // パス表示
                ImGui::LabelText("Path", "%s", s->scriptPath.c_str());

                // ファイル選択ダイアログ
                if (ImGui::Button("Load Script...")) {
                    char filename[256] = {};
                    // 初期フォルダをData/Scriptにしておくと便利かもしれません
                    if (Dialog::OpenFileName(filename,
                            256,
                            "Lua Script\0*.lua\0All Files\0*.*\0",
                            "Select Script",
                            Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {

                        // ★絶対パス -> 相対パス(Data/...) 変換ロジック
                        namespace fs         = std::filesystem;
                        fs::path absPath     = filename;           // 選択された絶対パス
                        fs::path currentPath = fs::current_path(); // 実行ファイルの場所

                        std::error_code ec;
                        fs::path        relPath = fs::relative(absPath, currentPath, ec);

                        if (!ec && !relPath.empty()) {
                            // generic_string() を使うと Windowsでも「/」区切りになります
                            // 例: "Data/Script/Gate.lua"
                            s->scriptPath = relPath.generic_string();
                        }
                        else {
                            // 変換失敗時（別ドライブなど）はそのまま
                            s->scriptPath = filename;
                        }

                        // パスが変わったらリロードフラグを立てる
                        s->isLoaded = false;
                    }
                }

                ImGui::SameLine();

                // 強制リロードボタン (編集中のスクリプトを反映させるため)
                if (ImGui::Button("Reload")) {
                    s->isLoaded = false;
                }

                // 状態表示
                if (s->isLoaded) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Status: Loaded");
                }
                else {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Status: Waiting load...");
                }
            });

     
}

// 【変更】 Draw関数にも EntityID と World* を追加して渡す
void ComponentGuiRegistry::Draw(
    TypeID id, void *data, EntityID entity, CCL::ECS::Core::World *world)
{
    auto it = _registry.find(id);
    if (it != _registry.end()) {
        it->second.guiFunc(data, entity, world);
    }
    else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No GUI registered for this ID.");
    }
}