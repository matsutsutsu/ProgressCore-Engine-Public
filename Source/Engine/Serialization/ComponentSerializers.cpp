#include "ComponentSerializers.h"
#include "ComponentRegistry.h"
#include "Game/Core/AllComponents.h" // ここでのみ重いヘッダーをインクルード
#include "SerializationContext.h"

#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include <DirectXMath.h>
#include <algorithm>

using json = nlohmann::json;
using namespace DirectX;
using namespace CCL::ECS;
using namespace CCL::ECS::Core;

#include <Windows.h> 
#include <filesystem> // 追加
namespace fs = std::filesystem;

// 追加・置き換え: Shift-JISのパスを受け取り、相対パス化してUTF-8で返す
// ヘルパー: Shift-JIS(Windowsパス) -> UTF-8(JSON) ＆ 相対パス化
std::string PathToRelativeUtf8(const std::string &pathSjis)
{
    if (pathSjis.empty()) return "";

    // 1. Shift-JIS -> wstring (Windowsパスとして扱うため)
    int          len = MultiByteToWideChar(CP_ACP, 0, pathSjis.c_str(), -1, NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_ACP, 0, pathSjis.c_str(), -1, &wstr[0], len);
    wstr.resize(len - 1); // 末尾の\0除去

    // 2. 絶対パス -> カレントディレクトリからの相対パス に変換
    std::error_code ec;
    fs::path        targetPath(wstr);
    fs::path        currentPath = fs::current_path();
    fs::path        relPath     = fs::relative(targetPath, currentPath, ec);
    if (ec) relPath = targetPath; // 失敗時(別ドライブ等)はそのまま

    // 3. wstring -> UTF-8 (JSON保存用)
    // generic_string() で区切り文字を '/' に統一しておくとJSONで扱いやすい
    std::string u8str = relPath.generic_u8string();
    return u8str;
}

// ヘルパー: UTF-8(JSON) -> Shift-JIS(Windowsパス)
std::string Utf8ToSjis(const std::string &pathUtf8)
{
    if (pathUtf8.empty()) return "";

    // 1. UTF-8 -> wstring
    int          len = MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, NULL, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, &wstr[0], len);
    wstr.resize(len - 1);

    // 2. wstring -> Shift-JIS
    int         lenSjis = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string sjis(lenSjis, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &sjis[0], lenSjis, NULL, NULL);
    sjis.resize(lenSjis - 1);

    return sjis;
}

// =========================================================
//  シリアライズ簡略化マクロ (このファイル内のみ有効)
// =========================================================
#define J_SAVE(var) j[#var] = obj.var
#define J_LOAD(var, defaultVal) obj.var = j.value(#var, defaultVal)
#define J_LOAD_OPT(var)                                                                            \
    if (j.contains(#var)) j.at(#var).get_to(obj.var)
#define J_LOAD_ENUM(var, type, defaultVal)                                                         \
    obj.var = static_cast<type>(j.value(#var, static_cast<int>(defaultVal)))

// ====================================================================================
//  DirectX Math Helpers
//  ParticleSystemConfigの変換で使われるため、ここに実装が必要です
// ====================================================================================
namespace DirectX {
    inline void to_json(json &j, const XMFLOAT2 &v) { j = json::array({v.x, v.y}); }
    inline void from_json(const json &j, XMFLOAT2 &v)
    {
        if (j.is_array() && j.size() >= 2) {
            v.x = j[0];
            v.y = j[1];
        }
    }

    inline void to_json(json &j, const XMFLOAT3 &v) { j = json::array({v.x, v.y, v.z}); }
    inline void from_json(const json &j, XMFLOAT3 &v)
    {
        if (j.is_array() && j.size() >= 3) {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
        }
    }

    inline void to_json(json &j, const XMFLOAT4 &v) { j = json::array({v.x, v.y, v.z, v.w}); }
    inline void from_json(const json &j, XMFLOAT4 &v)
    {
        if (j.is_array() && j.size() >= 4) {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
            v.w = j[3];
        }
    }

    inline void to_json(json &j, const XMUINT2 &v) { j = json::array({v.x, v.y}); }
    inline void from_json(const json &j, XMUINT2 &v)
    {
        if (j.is_array() && j.size() >= 2) {
            v.x = j[0];
            v.y = j[1];
        }
    }
} // namespace DirectX

// ====================================================================================
//  Global Implementations (ヘッダーで宣言したもの)
// ====================================================================================

// ★ ParticleSystemConfig の to_json / from_json 実装
// これにより Prefab.cpp の get_to(config) がリンクできるようになります
void to_json(json &j, const ParticleSystemConfig &c)
{
    j = json{{"emission_position", c.emission_position},
        {"emission_offset", c.emission_offset},
        {"emission_size", c.emission_size},
        {"emission_cone_angle", c.emission_cone_angle},
        {"emission_speed", c.emission_speed},
        {"emission_angular_speed", c.emission_angular_speed},
        {"emission_mode", c.emission_mode},
        {"burst_count", c.burst_count},
        {"burst_trigger", c.burst_trigger},
        {"lifespan", c.lifespan},
        {"spawn_delay", c.spawn_delay},
        {"fade_duration", c.fade_duration},
        {"manual_color", c.manual_color},
        {"color_mode", c.color_mode},
        {"max_particle_count", c.max_particle_count},
        {"type", c.type},
        {"gravity", c.gravity},
        {"velocity_stretch", c.velocity_stretch},
        {"noise_scale", c.noise_scale},
        {"noise_strength", c.noise_strength},
        {"particle_scale", c.particle_scale},
        {"particle_offset_y", c.particle_offset_y},
        {"emission_stretch_x", c.emission_stretch_x},
        {"emission_stretch_z", c.emission_stretch_z},
        {"uv_scroll_speed", c.uv_scroll_speed},
        {"sprite_anim_mode", c.sprite_anim_mode},
        {"sprite_sheet_grid", c.sprite_sheet_grid},
        {"render_mode", c.render_mode}};
}

void from_json(const json &j, ParticleSystemConfig &c)
{
    if (j.contains("emission_position")) j["emission_position"].get_to(c.emission_position);
    if (j.contains("emission_offset")) j["emission_offset"].get_to(c.emission_offset);
    if (j.contains("emission_size")) j["emission_size"].get_to(c.emission_size);
    if (j.contains("emission_cone_angle")) j["emission_cone_angle"].get_to(c.emission_cone_angle);
    if (j.contains("emission_speed")) j["emission_speed"].get_to(c.emission_speed);
    if (j.contains("emission_angular_speed"))
        j["emission_angular_speed"].get_to(c.emission_angular_speed);

    if (j.contains("emission_mode")) c.emission_mode = j["emission_mode"];
    if (j.contains("burst_count")) c.burst_count = j["burst_count"];
    if (j.contains("burst_trigger")) c.burst_trigger = j["burst_trigger"];

    if (j.contains("lifespan")) j["lifespan"].get_to(c.lifespan);
    if (j.contains("spawn_delay")) j["spawn_delay"].get_to(c.spawn_delay);
    if (j.contains("fade_duration")) j["fade_duration"].get_to(c.fade_duration);
    if (j.contains("manual_color")) j["manual_color"].get_to(c.manual_color);
    if (j.contains("color_mode")) c.color_mode = j["color_mode"];

    if (j.contains("max_particle_count")) c.max_particle_count = j["max_particle_count"];
    if (j.contains("type")) c.type = j["type"];

    if (j.contains("gravity")) c.gravity = j["gravity"];
    if (j.contains("velocity_stretch")) c.velocity_stretch = j["velocity_stretch"];
    if (j.contains("noise_scale")) c.noise_scale = j["noise_scale"];
    if (j.contains("noise_strength")) c.noise_strength = j["noise_strength"];

    if (j.contains("particle_scale")) j["particle_scale"].get_to(c.particle_scale);
    if (j.contains("particle_offset_y")) c.particle_offset_y = j["particle_offset_y"];
    if (j.contains("emission_stretch_x")) c.emission_stretch_x = j["emission_stretch_x"];
    if (j.contains("emission_stretch_z")) c.emission_stretch_z = j["emission_stretch_z"];

    if (j.contains("uv_scroll_speed")) j["uv_scroll_speed"].get_to(c.uv_scroll_speed);
    if (j.contains("sprite_anim_mode")) c.sprite_anim_mode = j["sprite_anim_mode"];
    if (j.contains("sprite_sheet_grid")) j["sprite_sheet_grid"].get_to(c.sprite_sheet_grid);
    if (j.contains("render_mode")) c.render_mode = j["render_mode"];
}

namespace ComponentSerializers {

    // =========================================================
    //  個別実装 (Serialize / Deserialize)
    // =========================================================

    // --- NameComponent ---
    void Serialize(json &j, const NameComponent &c) { j["name"] = c.name; }
    void Deserialize(const json &j, NameComponent &c)
    {
        std::string str = j.value("name", "Entity");
        strncpy_s(c.name, str.c_str(), sizeof(c.name) - 1);
    }

    // --- TransformComponent ---
    void Serialize(json &j, const TransformComponent &obj)
    {
        J_SAVE(position);
        J_SAVE(rotation);
        J_SAVE(scale);
        if (obj.parentID != 0) j["parentID"] = obj.parentID;
    }
    void Deserialize(const json &j, TransformComponent &obj)
    {
        J_LOAD(position, XMFLOAT3(0, 0, 0));
        J_LOAD(rotation, XMFLOAT4(0, 0, 0, 1));
        J_LOAD(scale, XMFLOAT3(1, 1, 1));
        if (j.contains("parentID")) {
            obj.parentID = SerializationContext::ResolveID(j["parentID"]);
        }
        else {
            obj.parentID = 0;
        }
    }


    // --- BoxColliderComponent ---
    void Serialize(json &j, const BoxColliderComponent &obj)
    {
        J_SAVE(size);
        J_SAVE(center);
        j["layer"] = static_cast<uint32_t>(obj.layer);
        j["mask"]  = static_cast<uint32_t>(obj.mask);
    }
    void Deserialize(const json &j, BoxColliderComponent &obj)
    {
        J_LOAD_OPT(size);
        J_LOAD_OPT(center);
        if (j.contains("layer")) obj.layer = static_cast<Layer>(j["layer"].get<uint32_t>());
        if (j.contains("mask")) obj.mask = static_cast<Layer>(j["mask"].get<uint32_t>());
    }

    // --- SphereColliderComponent ---
    void Serialize(json &j, const SphereColliderComponent &obj)
    {
        J_SAVE(radius);
        j["layer"] = static_cast<uint32_t>(obj.layer);
        j["mask"]  = static_cast<uint32_t>(obj.mask);
    }
    void Deserialize(const json &j, SphereColliderComponent &obj)
    {
        J_LOAD(radius, 0.5f);
        if (j.contains("layer")) obj.layer = static_cast<Layer>(j["layer"].get<uint32_t>());
        if (j.contains("mask")) obj.mask = static_cast<Layer>(j["mask"].get<uint32_t>());
    }

    // --- CapsuleColliderComponent ---
    void Serialize(json &j, const CapsuleColliderComponent &obj)
    {
        J_SAVE(radius);
        J_SAVE(height);
        J_SAVE(center);
        j["layer"] = static_cast<uint32_t>(obj.layer);
        j["mask"]  = static_cast<uint32_t>(obj.mask);
    }
    void Deserialize(const json &j, CapsuleColliderComponent &obj)
    {
        J_LOAD(radius, 0.5f);
        J_LOAD(height, 2.0f);
        J_LOAD_OPT(center);
        if (j.contains("layer")) obj.layer = static_cast<Layer>(j["layer"].get<uint32_t>());
        if (j.contains("mask")) obj.mask = static_cast<Layer>(j["mask"].get<uint32_t>());
    }


    // --- ModelComponent ---
    void Serialize(json &j, const ModelComponent &obj) { j = json{{"path", obj.assetPath}}; }
    void Deserialize(const json &j, ModelComponent &obj)
    {
        std::string path = j.value("path", "");
        if (!path.empty()) {
            const_cast<ModelComponent &>(obj).SetModel(path);
        }
    }

    // --- StaticMeshComponent ---
    void Serialize(json &j, const StaticMeshComponent &obj) { J_SAVE(isLargeMesh); }
    void Deserialize(const json &j, StaticMeshComponent &obj) { J_LOAD(isLargeMesh, false); }

    // --- MaterialComponent ---
    void Serialize(json &j, const MaterialComponent &obj) { /* Empty */ }
    void Deserialize(const json &j, MaterialComponent &obj) { /* Empty */ }

    // --- PrimitiveComponent ---
    void Serialize(json &j, const PrimitiveComponent &obj)
    {
        J_SAVE(type);
        J_SAVE(size);
        J_SAVE(radius);
        J_SAVE(height);
        J_SAVE(color);
    }
    void Deserialize(const json &j, PrimitiveComponent &obj)
    {
        J_LOAD_ENUM(type, PrimitiveType, PrimitiveType::Box);
        J_LOAD_OPT(size);
        J_LOAD(radius, 0.5f);
        J_LOAD(height, 1.0f);
        J_LOAD_OPT(color);
    }

    // --- DirectionalLightComponent ---
    void Serialize(json &j, const DirectionalLightComponent &obj)
    {
        J_SAVE(color);
        J_SAVE(intensity);
        J_SAVE(skyColor);
        J_SAVE(groundColor);
    }
    void Deserialize(const json &j, DirectionalLightComponent &obj)
    {
        J_LOAD_OPT(color);
        J_LOAD(intensity, 1.0f);
        J_LOAD_OPT(skyColor);
        J_LOAD_OPT(groundColor);
    }

    // --- PointLightComponent ---
    void Serialize(json &j, const PointLightComponent &obj)
    {
        J_SAVE(color);
        J_SAVE(intensity);
        J_SAVE(range);
        j["attenuation"] = {obj.constant, obj.linear, obj.quadratic};
    }
    void Deserialize(const json &j, PointLightComponent &obj)
    {
        J_LOAD_OPT(color);
        J_LOAD(intensity, 1.0f);
        J_LOAD(range, 10.0f);
        if (j.contains("attenuation") && j["attenuation"].is_array()) {
            obj.constant  = j["attenuation"][0];
            obj.linear    = j["attenuation"][1];
            obj.quadratic = j["attenuation"][2];
        }
    }

    // --- SpotLightComponent ---
    void Serialize(json &j, const SpotLightComponent &obj)
    {
        J_SAVE(color);
        J_SAVE(intensity);
        J_SAVE(range);
        J_SAVE(innerCos);
        J_SAVE(outerCos);
    }
    void Deserialize(const json &j, SpotLightComponent &obj)
    {
        J_LOAD_OPT(color);
        J_LOAD(intensity, 1.0f);
        J_LOAD(range, 10.0f);
        J_LOAD(innerCos, 0.9f);
        J_LOAD(outerCos, 0.8f);
    }


    // --- FogComponent ---
    void Serialize(json &j, const FogComponent &obj)
    {
        J_SAVE(color);
        J_SAVE(start);
        J_SAVE(end);

        // 高さフォグ
        J_SAVE(heightStart);
        J_SAVE(heightDensity);

        // ノイズフォグ: パスがある場合のみ保存 (GPUParticle参考 + 相対パスUTF-8変換)
        if (obj.noiseTexturePath[0] != '\0') {
            j["noisePath"] = PathToRelativeUtf8(obj.noiseTexturePath);
        }

        J_SAVE(noiseScale);
        J_SAVE(noiseStrength);
        J_SAVE(noiseSpeed);

        J_SAVE(rimColor);
        J_SAVE(rimPower);
        J_SAVE(rimStrength);

        J_SAVE(center);
        J_SAVE(enabled);
    }

    void Deserialize(const json &j, FogComponent &obj)
    {
        J_LOAD_OPT(color);
        J_LOAD(start, 10.0f);
        J_LOAD(end, 50.0f);

        J_LOAD(heightStart, 0.0f);
        J_LOAD(heightDensity, 0.5f);

        // ノイズフォグ読み込み: UTF-8 -> Shift-JIS に戻してロード
        std::string pathUtf8 = j.value("noisePath", "");
        if (!pathUtf8.empty()) {
            std::string pathSjis = Utf8ToSjis(pathUtf8);

            // バッファサイズチェック
            if (pathSjis.length() < 256) {
                strcpy_s(obj.noiseTexturePath, pathSjis.c_str());

                // テクスチャを即座にロード
                // (GPUParticleはInitializeで行っていますが、Fogはここで行います)
                GpuResourceUtils::LoadTexture(Graphics::Instance().GetDevice(),
                    obj.noiseTexturePath,
                    obj.noiseTextureSRV.GetAddressOf());
            }
        }

        J_LOAD(noiseScale, 0.1f);
        J_LOAD(noiseStrength, 0.0f);
        J_LOAD_OPT(noiseSpeed);

        J_LOAD(rimColor, XMFLOAT3(1.0f, 1.0f, 1.0f));
        J_LOAD(rimPower, 3.0f);
        J_LOAD(rimStrength, 0.0f);

        J_LOAD_OPT(center);
        J_LOAD(enabled, true);
    }

    // --- ShadowMapConfigComponent ---
    void Serialize(json &j, const ShadowMapConfigComponent &obj)
    {
        J_SAVE(shadowBias);
        J_SAVE(shadowColor);
        J_SAVE(orthoSize);
    }
    void Deserialize(const json &j, ShadowMapConfigComponent &obj)
    {
        J_LOAD(shadowBias, 0.001f);
        J_LOAD_OPT(shadowColor);
        J_LOAD(orthoSize, 30.0f);
    }

    // --- BloomConfigComponent ---
    void Serialize(json &j, const BloomConfigComponent &obj)
    {
        J_SAVE(enable);
        J_SAVE(threshold);
        J_SAVE(intensity);
        J_SAVE(softKnee);
        J_SAVE(radius);
    }
    void Deserialize(const json &j, BloomConfigComponent &obj)
    {
        J_LOAD(enable, true);
        J_LOAD(threshold, 1.0f);
        J_LOAD(intensity, 1.0f);
        J_LOAD(softKnee, 0.5f);
        J_LOAD(radius, 1.0f);
    }

    // --- ToneMapConfigComponent ---
    void Serialize(json &j, const ToneMapConfigComponent &obj) { J_SAVE(exposure); }
    void Deserialize(const json &j, ToneMapConfigComponent &obj) { J_LOAD(exposure, 1.0f); }

    // --- PlayerComponent ---
    void Serialize(json &j, const PlayerComponent &obj)
    {
        J_SAVE(moveSpeed);
        J_SAVE(turnSpeed);
        J_SAVE(padSlot);
    }
    void Deserialize(const json &j, PlayerComponent &obj)
    {
        J_LOAD(moveSpeed, 15.0f);
        J_LOAD(turnSpeed, 10.0f);
        J_LOAD(padSlot, 0);
    }

    // --- HealthComponent ---
    void Serialize(json &j, const HealthComponent &obj)
    {
        J_SAVE(maxHealth);
        J_SAVE(currentHealth);
        J_SAVE(team);
        J_SAVE(invincibilityDuration);
    }
    void Deserialize(const json &j, HealthComponent &obj)
    {
        J_LOAD(maxHealth, 100.0f);
        J_LOAD(currentHealth, 100.0f);
        J_LOAD_ENUM(team, TeamID, TeamID::Player);
        J_LOAD(invincibilityDuration, 0.5f);
    }


    // --- GPUParticleComponent ---
    void Serialize(json &j, const GPUParticleComponent &obj)
    {
        // グローバル関数 to_json を呼び出す
        ::to_json(j["config"], obj.config);
        j["maxParticles"] = obj.maxParticles;
        if (obj.texturePath[0] != '\0') j["texturePath"] = std::string(obj.texturePath);
        if (obj.colorRampPath[0] != '\0') j["colorRampPath"] = std::string(obj.colorRampPath);
    }
    void Deserialize(const json &j, GPUParticleComponent &obj)
    {
        // グローバル関数 from_json を呼び出す
        if (j.contains("config")) ::from_json(j["config"], obj.config);

        int         maxP = j.value("maxParticles",
            obj.config.max_particle_count > 0 ? obj.config.max_particle_count : 10000);
        std::string tex  = j.value("texturePath", "");
        std::string ramp = j.value("colorRampPath", "");
        if (!tex.empty()) strcpy_s(obj.texturePath, tex.c_str());
        if (!ramp.empty()) strcpy_s(obj.colorRampPath, ramp.c_str());
        // 数値を直接代入し、初期化フラグを折るだけ！
        obj.maxParticles              = maxP;
        obj.config.max_particle_count = maxP;
        obj.isInitialized             = false;
    }

    // --- PooledParticleComponent ---
    void Serialize(json &j, const PooledParticleComponent &obj)
    {
        j["poolKey"] = obj.poolKey;
        J_SAVE(isSleeping);
    }
    void Deserialize(const json &j, PooledParticleComponent &obj)
    {
        std::string key = j.value("poolKey", "");
        if (!key.empty()) strcpy_s(obj.poolKey, key.c_str());
        J_LOAD(isSleeping, false);
    }

    // --- AnimatorComponent ---
    void Serialize(json &j, const AnimatorComponent &c)
    {
        j = json{
            {"currentAnimIndex", c.currentAnimIndex},
            {"playbackSpeed", c.playbackSpeed},
            {"isLoop", c.isLoop},
            // 必要ならタイマーも保存するが、通常は初期状態(0)で良い
            // {"currentTimer", c.currentTimer}
        };
    }

    void Deserialize(const json &j, AnimatorComponent &c)
    {
        c.currentAnimIndex = j.value("currentAnimIndex", -1);
        c.playbackSpeed    = j.value("playbackSpeed", 1.0f);
        c.isLoop           = j.value("isLoop", true);
    }

    // =========================================================
    // Virtual Camera Components
    // =========================================================

    // --- VirtualCamera (基本設定) ---
    void Serialize(json &j, const VirtualCamera &obj)
    {
        J_SAVE(priority);
        J_SAVE(blendTime);
        J_SAVE(fov);
        J_SAVE(nearClip);
        J_SAVE(farClip);
        // resultPos などの計算結果は保存不要
    }
    void Deserialize(const json &j, VirtualCamera &obj)
    {
        J_LOAD(priority, 0);
        J_LOAD(blendTime, 1.0f);
        J_LOAD(fov, 45.0f);
        J_LOAD(nearClip, 0.1f);
        J_LOAD(farClip, 1000.0f);
    }

    // --- CameraBodyFollow (追従設定) ---
    void Serialize(json &j, const CameraBodyFollow &obj)
    {
        if (obj.target != 0) j["target"] = obj.target;
        J_SAVE(offset);
        J_SAVE(damping);
        J_SAVE(lockY);
    }
    void Deserialize(const json &j, CameraBodyFollow &obj)
    {
        if (j.contains("target"))
            obj.target = SerializationContext::ResolveID(j["target"]);
        else
            obj.target = 0;

        J_LOAD_OPT(offset);
        J_LOAD(damping, 5.0f);
        J_LOAD(lockY, false);
    }

    // --- CameraAimLookAt (注視設定) ---
    void Serialize(json &j, const CameraAimLookAt &obj)
    {
        if (obj.target != 0) j["target"] = obj.target;
        J_SAVE(offset);
        J_SAVE(damping);
    }
    void Deserialize(const json &j, CameraAimLookAt &obj)
    {
        if (j.contains("target"))
            obj.target = SerializationContext::ResolveID(j["target"]);
        else
            obj.target = 0;

        J_LOAD_OPT(offset);
        J_LOAD(damping, 10.0f);
    }

    // --- CameraShake ---
    void Serialize(json &j, const CameraShake &obj)
    {
        J_SAVE(amplitude);
        J_SAVE(duration);
        J_SAVE(maxDuration);
    }
    void Deserialize(const json &j, CameraShake &obj)
    {
        J_LOAD(amplitude, 0.0f);
        J_LOAD(duration, 0.0f);
        J_LOAD(maxDuration, 1.0f);
    }

        // --- CameraBodyFree ---
    void Serialize(json &j, const CameraBodyFree &obj)
    {
        J_SAVE(moveSpeed);
        J_SAVE(lookSpeed);
        J_SAVE(currentYaw);
        J_SAVE(currentPitch);
    }
    void Deserialize(const json &j, CameraBodyFree &obj)
    {
        J_LOAD(moveSpeed, 10.0f);
        J_LOAD(lookSpeed, 0.2f);
        J_LOAD(currentYaw, 0.0f);
        J_LOAD(currentPitch, 0.0f);
    }


    // ---------------------------------------------------------
    // TriggerComponent
    // ---------------------------------------------------------
    void Serialize(json &j, const TriggerComponent &obj)
    {
        J_SAVE(radius);
        J_SAVE(isOneShot);
        // hasTriggered はランタイム状態なので保存しない（ロード時はfalseでよい）
    }

    void Deserialize(const json &j, TriggerComponent &obj)
    {
        J_LOAD(radius, 3.0f);
        J_LOAD(isOneShot, false);
        obj.hasTriggered = false; // 初期化
    }

    // ---------------------------------------------------------
    // LuaScriptComponent
    // ---------------------------------------------------------
    void Serialize(json &j, const LuaScriptComponent &obj)
    {
        // パスを相対パス＆UTF-8化して保存
        if (!obj.scriptPath.empty()) {
            j["path"] = PathToRelativeUtf8(obj.scriptPath);
        }
    }

    void Deserialize(const json &j, LuaScriptComponent &obj)
    {
        // UTF-8(JSON) -> Shift-JIS(Windows) に戻して読み込み
        std::string pathUtf8 = j.value("path", "");
        if (!pathUtf8.empty()) {
            obj.scriptPath = Utf8ToSjis(pathUtf8);
        }
        obj.isLoaded = false; // ロード直後は未コンパイル状態にする
    }

    // =========================================================
    //  一括登録ロジック
    // =========================================================

    // 登録ヘルパーテンプレート (ローカル)
    template <typename T> void RegisterT(const std::string &name)
    {
        ComponentRegistry::MetaInfo info;
        info.name = name;

        // Serialize
        info.serialize = [](json &j, const void *ptr) {
            Serialize(j, *static_cast<const T *>(ptr));
        };

        // DeserializeToWorld
        info.deserializeToWorld = [](World *world, EntityID entity, const json &j) {
            T temp;
            Deserialize(j, temp);
            world->AddComponent<T>(entity, std::move(temp));
        };

        ComponentRegistry::Instance().RegisterInternal(TypeInfo<T>::ID(), name, std::move(info));
    }

    void RegisterAll()
    {
        RegisterT<NameComponent>("Name");
        RegisterT<TransformComponent>("Transform");

        RegisterT<BoxColliderComponent>("BoxCollider");
        RegisterT<SphereColliderComponent>("SphereCollider");
        RegisterT<CapsuleColliderComponent>("CapsuleCollider");

        RegisterT<ModelComponent>("Model");
        RegisterT<StaticMeshComponent>("StaticMesh");
        RegisterT<MaterialComponent>("Material");
        RegisterT<PrimitiveComponent>("Primitive");
        RegisterT<DirectionalLightComponent>("DirectionalLight");
        RegisterT<PointLightComponent>("PointLight");
        RegisterT<SpotLightComponent>("SpotLight");
        RegisterT<FogComponent>("Fog");
        RegisterT<ShadowMapConfigComponent>("ShadowMapConfig");
        RegisterT<BloomConfigComponent>("BloomConfig");
        RegisterT<ToneMapConfigComponent>("ToneMapConfig");
        RegisterT<PlayerComponent>("Player");

        RegisterT<HealthComponent>("Health");

        // 【After：メタデータ駆動の登録へ差し替え】
        //ComponentMetaJson::RegisterMetaT<EnemyComponent>(ComponentRegistry::Instance(), "Enemy");
        //RegisterT<EnemyComponent>("Enemy");

        RegisterT<GPUParticleComponent>("GPUParticle");
      
        RegisterT<PooledParticleComponent>("PooledParticle");
        RegisterT<AnimatorComponent>("Animator");
        RegisterT<VirtualCamera>("VirtualCamera");
        RegisterT<CameraBodyFollow>("CameraBodyFollow");
        RegisterT<CameraAimLookAt>("CameraAimLookAt");
        RegisterT<CameraShake>("CameraShake");
        RegisterT<CameraBodyFree>("CameraBodyFree");


        // ★これを追加
        RegisterT<TriggerComponent>("Trigger");
        RegisterT<LuaScriptComponent>("LuaScript");

    }
} // namespace ComponentSerializers