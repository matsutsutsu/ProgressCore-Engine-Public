#include "Game/Logics/Stage/DebrisFieldSystem.h"

#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Time/TimeService.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <json.hpp>
#include <tracy/Tracy.hpp>

using namespace DirectX;
using json = nlohmann::json;

namespace
{
    constexpr float kTwoPi = XM_2PI;
} // namespace

void DebrisFieldSystem::Update(float dt)
{
    // 描画ステージはポーズ中も実時間で走るので、PresentationDt でポーズ中は 0 にする。
    // その上で TimeState の倍率を掛け、スローやヒットストップで帯も一緒に遅くする
    const float presentDt = Time::PresentationDt(*m_world, dt);

    uint32_t submitted = 0;
    ForEach([&](DebrisFieldComponent &field, const TransformComponent &transform, const TimeStateComponent &time) {
        RebuildIfChanged(field);

        field.elapsed += presentDt * time.scale;

        SubmitRocks(field, transform);
        submitted += static_cast<uint32_t>(field.rocks.size());
    });
    m_submittedLastFrame = submitted;
}

void DebrisFieldSystem::OnGui()
{
    ImGui::Text("積んだ岩: %u", m_submittedLastFrame);

    // プレハブの scale やモデルを変えたときに、シーンを読み直さずに反映する
    if (ImGui::Button("プレハブを読み直す"))
    {
        ForEach([](DebrisFieldComponent &field, const TransformComponent &, const TimeStateComponent &) {
            field.builtFolder.clear();
        });
    }
}

// ---------------------------------------------------------------------------
// 岩の種類と乱数
// ---------------------------------------------------------------------------

void DebrisFieldSystem::RebuildIfChanged(DebrisFieldComponent &field)
{
    if (field.builtFolder != field.prefabFolder)
    {
        LoadKinds(field);
        field.builtFolder = field.prefabFolder;
        field.builtCount  = -1; // 種類の数が変わると kind の添字が合わなくなるので、乱数も作り直す
    }

    if (field.builtCount != field.count || field.builtSeed != field.seed)
    {
        BuildRocks(field);
        field.builtCount = field.count;
        field.builtSeed  = field.seed;
    }
}

void DebrisFieldSystem::LoadKinds(DebrisFieldComponent &field)
{
    field.kinds.clear();

    // プレハブ自体は生成せず、JSON から Model と scale だけを読む。
    // 岩の見た目はプレハブを正とし、プレハブを直せば帯の岩も変わるようにするため
    const auto paths = FileSystem::ListFiles(field.prefabFolder, ".json");
    for (const auto &path : paths)
    {
        const json root = json::parse(FileSystem::ReadAllText(path), nullptr, false);
        if (root.is_discarded() || !root.contains("entities") || !root["entities"].is_array()) continue;

        // プレハブの中で、最初に Model を持つ Entity を岩として使う
        for (const auto &entity : root["entities"])
        {
            if (!entity.contains("components")) continue;
            const auto &comps = entity["components"];
            if (!comps.contains("Model")) continue;

            const std::string modelPath = comps["Model"].value("assetPath", std::string());
            if (modelPath.empty()) continue;

            DebrisKind &kind       = field.kinds.emplace_back();
            kind.model.generateLod = comps["Model"].value("generateLod", false); // SetModel より前に決める
            kind.model.SetModel(modelPath.c_str());

            if (comps.contains("Transform") && comps["Transform"].contains("scale"))
            {
                const auto &s = comps["Transform"]["scale"];
                if (s.is_array() && s.size() >= 3)
                {
                    kind.scale = {s[0].get<float>(), s[1].get<float>(), s[2].get<float>()};
                }
            }
            break;
        }
    }

    if (field.kinds.empty())
    {
        CCL_LOG_WARN(LogCategory::Game, "DebrisFieldSystem: %s に Model を持つプレハブがありません。",
                     field.prefabFolder.c_str());
    }
}

void DebrisFieldSystem::BuildRocks(DebrisFieldComponent &field)
{
    // 種類が無いと kind の添字が指す先が無いので、岩も作らない
    if (field.kinds.empty())
    {
        field.rocks.clear();
        return;
    }

    const int count = (std::max)(field.count, 0);
    field.rocks.resize(static_cast<size_t>(count));

    const size_t kindCount = field.kinds.size();
    for (int i = 0; i < count; ++i)
    {
        // 岩1個につき乱数を10個使う。番号ごとに16ずつずらし、隣の岩と値が重ならないようにする
        const uint32_t base = static_cast<uint32_t>(field.seed) * 0x9E3779B9u + static_cast<uint32_t>(i) * 16u;
        auto           rand = [base](uint32_t k) { return Hash01(base + k); };

        DebrisRock &rock = field.rocks[i];
        rock.kind        = static_cast<uint16_t>((std::min)(static_cast<size_t>(rand(0) * kindCount), kindCount - 1));
        rock.angle0      = rand(1) * kTwoPi;
        rock.radiusT     = rand(2);
        rock.heightT     = rand(3);
        rock.speedT      = rand(5) * 2.0f - 1.0f;
        rock.spinT       = rand(6) * 2.0f - 1.0f;
        rock.phase       = rand(7) * kTwoPi;

        // 2乗して小さい岩を多くする。同じ数ずつだと大きい岩ばかり目に付き、がれきに見えない
        rock.scaleT = rand(4) * rand(4);

        // 球面上に一様な向きを軸にする
        const float z   = rand(8) * 2.0f - 1.0f;
        const float phi = rand(9) * kTwoPi;
        const float rxy = std::sqrt((std::max)(0.0f, 1.0f - z * z));
        rock.spinAxis   = {rxy * std::cos(phi), z, rxy * std::sin(phi)};
    }
}

float DebrisFieldSystem::Hash01(uint32_t seed)
{
    seed ^= seed >> 15;
    seed *= 2246822519u;
    seed ^= seed >> 13;
    seed *= 3266489917u;
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void DebrisFieldSystem::SubmitRocks(const DebrisFieldComponent &field, const TransformComponent &transform)
{
    ZoneScopedN("DebrisFieldSystem::SubmitRocks");

    // 処理フロー:
    // 1. 種類ごとに RenderCommand のひな形を作る
    // 2. 岩ごとに位置・回転・大きさを計算して積む

    // ---------------------------------------------------------
    // 1. ひな形
    // ---------------------------------------------------------

    // シェーダーの決め方は ModelRenderSystem と同じ（先頭のマテリアル、無ければ PBR）。
    // 同じモデルの岩が同じキーになり、OpaquePass で1つのインスタンス描画にまとまる
    std::vector<RenderCommand> templates(field.kinds.size());
    for (size_t k = 0; k < field.kinds.size(); ++k)
    {
        RenderCommand &cmd = templates[k];
        cmd.model          = field.kinds[k].model.GetModel();
        cmd.assetHash      = field.kinds[k].model.assetHash;
        cmd.shaderHash     = "PBR"_hash;
        cmd.overrideCount  = 0;
        cmd.customParams   = {0.0f, 0.0f, 0.0f, 0.0f};

        if (cmd.model)
        {
            const auto &materials = cmd.model->GetMaterials();
            if (!materials.empty() && materials[0].data)
            {
                cmd.shaderHash = materials[0].data->shaderHash;
            }
        }
    }

    // ---------------------------------------------------------
    // 2. 岩ごとの行列
    // ---------------------------------------------------------

    // 帯の中心と傾きはこの Entity の行列で決まる。Entity を傾ければ帯も傾く
    const XMMATRIX fieldWorld = XMLoadFloat4x4(&transform.renderWorldMatrix);

    const float t        = field.elapsed;
    const float orbitRad = XMConvertToRadians(field.orbitSpeed);
    const float spinRad  = XMConvertToRadians(field.spinSpeedMax);
    const float bobOmega = kTwoPi * field.bobFrequency;
    auto       &queue    = RenderQueue::Instance();

    for (const DebrisRock &rock : field.rocks)
    {
        const RenderCommand &tmpl = templates[rock.kind];
        if (!tmpl.model) continue; // まだ読み込めていない種類は飛ばす

        const float radius = std::lerp(field.radiusMin, field.radiusMax, rock.radiusT);
        const float height = std::lerp(field.heightMin, field.heightMax, rock.heightT);
        const float scale  = std::lerp(field.scaleMin, field.scaleMax, rock.scaleT);

        // 角度は前フレームへの足し込みではなく経過時間から出す。足し込むと誤差がたまって帯の形が崩れる
        const float angle = rock.angle0 + orbitRad * (1.0f + field.speedJitter * rock.speedT) * t;
        const float bob   = field.bobAmplitude * std::sin(bobOmega * t + rock.phase);
        const float spin  = spinRad * rock.spinT * t + rock.phase;

        const XMFLOAT3 &baseScale = field.kinds[rock.kind].scale;
        const XMMATRIX  local =
            XMMatrixScaling(baseScale.x * scale, baseScale.y * scale, baseScale.z * scale) *
            XMMatrixRotationNormal(XMLoadFloat3(&rock.spinAxis), spin) *
            XMMatrixTranslation(std::cos(angle) * radius, height + bob, std::sin(angle) * radius);

        RenderCommand cmd = tmpl;
        XMStoreFloat4x4(&cmd.worldMatrix, local * fieldWorld);
        queue.SubmitOpaque(cmd);
    }
}

REGISTER_RENDER_SYSTEM(DebrisFieldSystem, Priority::RenderStage::R08_Main);
