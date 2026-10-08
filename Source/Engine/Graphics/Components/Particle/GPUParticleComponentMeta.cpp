// GPUParticleComponent のシリアライズ/インスペクタ定義。

#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include <json.hpp>
#include <cmath>
#include <string>
#include <cstring>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

using json = nlohmann::json;

// =====================================================================
// ParticleEmitterDesc <-> json 変換ロジック
// =====================================================================
namespace
{
    inline float SafeF(float v) { return std::isfinite(v) ? v : 0.0f; }

    void WriteDescToJson(json &j, const ParticleEmitterDesc &d)
    {
        j["emitRadius"]    = {SafeF(d.emitRadius.x), SafeF(d.emitRadius.y)};
        j["emitSize"]      = {SafeF(d.emitSize.x), SafeF(d.emitSize.y)};
        j["sizeRandom"]    = SafeF(d.sizeRandom);
        j["sizeEase"]      = d.sizeEase;
        j["sizeEasePower"] = SafeF(d.sizeEasePower);
        j["coneAngle"]     = {SafeF(d.coneAngle.x), SafeF(d.coneAngle.y)};
        j["emitSpeed"]     = {SafeF(d.emitSpeed.x), SafeF(d.emitSpeed.y)};
        j["angularSpeed"]  = {SafeF(d.angularSpeed.x), SafeF(d.angularSpeed.y)};
        j["lifespan"]      = {SafeF(d.lifespan.x), SafeF(d.lifespan.y)};
        j["spawnDelay"]    = {SafeF(d.spawnDelay.x), SafeF(d.spawnDelay.y)};
        j["fadeDuration"]  = {SafeF(d.fadeDuration.x), SafeF(d.fadeDuration.y)};
        j["localOffset"]   = {SafeF(d.localOffset.x), SafeF(d.localOffset.y), SafeF(d.localOffset.z)};
        j["emitStretch"]   = {SafeF(d.emitStretch.x), SafeF(d.emitStretch.y)};
        j["lineEnd"]       = {SafeF(d.lineEnd.x), SafeF(d.lineEnd.y), SafeF(d.lineEnd.z)};
        j["emitShape"]     = d.emitShape;
        j["uvScrollSpeed"] = {SafeF(d.uvScrollSpeed.x), SafeF(d.uvScrollSpeed.y)};
        j["scale"]         = {SafeF(d.scale.x), SafeF(d.scale.y)};
        j["color"]         = {SafeF(d.color.x), SafeF(d.color.y), SafeF(d.color.z), SafeF(d.color.w)};
        j["intensity"]     = SafeF(d.intensity);
        j["spriteGrid"]    = {d.spriteGrid.x, d.spriteGrid.y};

        j["localOffsetY"]    = SafeF(d.localOffsetY);
        j["gravity"]         = SafeF(d.gravity);
        j["maxSpeed"]        = SafeF(d.maxSpeed);
        j["velocityStretch"] = SafeF(d.velocityStretch);
        j["noiseScale"]      = SafeF(d.noiseScale);
        j["noiseStrength"]   = SafeF(d.noiseStrength);

        j["colorMode"]    = d.colorMode;
        j["renderMode"]   = d.renderMode;
        j["spriteMode"]   = d.spriteMode;
        j["orientMode"]   = d.orientMode;
        j["maxCount"]     = d.maxCount;
        j["particleType"] = d.particleType;
        j["emitMode"]     = d.emitMode;
        j["burstCount"]   = d.burstCount;

        j["attractorPos"]      = {d.attractorPos.x, d.attractorPos.y, d.attractorPos.z};
        j["attractorStrength"] = d.attractorStrength;
        j["attractorRadius"]   = d.attractorRadius;
        j["vortexStrength"]    = d.vortexStrength;
        j["drag"]              = d.drag;
        j["attractorLocal"]    = d.attractorLocal;

        j["name"]          = std::string(d.name);
        j["texturePath"]   = std::string(d.texturePath);
        j["colorRampPath"] = std::string(d.colorRampPath);
    }

    void ReadDescFromJson(const json &j, ParticleEmitterDesc &d)
    {
        auto F  = [&](const char *k, float &dst) { if (j.contains(k) && j[k].is_number()) dst = j[k].get<float>(); };
        auto I  = [&](const char *k, int &dst) { if (j.contains(k) && j[k].is_number()) dst = j[k].get<int>(); };
        auto F2 = [&](const char *k, GpuData::float2 &v) {
            if (j.contains(k) && j[k].is_array() && j[k].size() >= 2)
            {
                v.x = j[k][0].get<float>();
                v.y = j[k][1].get<float>();
            }
        };
        auto F3 = [&](const char *k, GpuData::float3 &v) {
            if (j.contains(k) && j[k].is_array() && j[k].size() >= 3)
            {
                v.x = j[k][0].get<float>();
                v.y = j[k][1].get<float>();
                v.z = j[k][2].get<float>();
            }
        };
        auto F4 = [&](const char *k, GpuData::float4 &v) {
            if (j.contains(k) && j[k].is_array() && j[k].size() >= 4)
            {
                v.x = j[k][0].get<float>();
                v.y = j[k][1].get<float>();
                v.z = j[k][2].get<float>();
                v.w = j[k][3].get<float>();
            }
        };
        auto STR = [&](const char *k, char *dst, size_t cap) {
            if (j.contains(k) && j[k].is_string()) strcpy_s(dst, cap, j[k].get<std::string>().c_str());
        };

        F2("emitRadius", d.emitRadius);
        F2("emitSize", d.emitSize);
        // sizeRandom が無い JSON は、既定の 1（0〜emitSize の一様ランダム）のまま読む。
        F("sizeRandom", d.sizeRandom);
        // sizeEase が無い JSON は既定の 0（線形）のまま読む。今までと同じ広がり方
        I("sizeEase", d.sizeEase);
        F("sizeEasePower", d.sizeEasePower);
        F2("coneAngle", d.coneAngle);
        F2("emitSpeed", d.emitSpeed);
        F2("angularSpeed", d.angularSpeed);
        F2("lifespan", d.lifespan);
        F2("spawnDelay", d.spawnDelay);
        F2("fadeDuration", d.fadeDuration);
        F3("localOffset", d.localOffset);
        F2("emitStretch", d.emitStretch);
        F2("uvScrollSpeed", d.uvScrollSpeed);
        F2("scale", d.scale);
        F4("color", d.color);
        // intensity が無い JSON は既定の 1 のまま読む（選んだ色そのままで、見た目は変わらない）。
        F("intensity", d.intensity);

        F("localOffsetY", d.localOffsetY);
        F("gravity", d.gravity);
        // maxSpeed が無い JSON は既定の 10 のまま読む。シェーダーに固定で入っていた値と同じなので、
        // 読み込むだけで見た目は変わらない。
        F("maxSpeed", d.maxSpeed);
        F("velocityStretch", d.velocityStretch);
        F("noiseScale", d.noiseScale);
        F("noiseStrength", d.noiseStrength);

        I("colorMode", d.colorMode);
        I("renderMode", d.renderMode);
        I("spriteMode", d.spriteMode);
        // orientMode が無い JSON は既定の 0（ビルボード）のまま読む。今までと同じ見た目
        I("orientMode", d.orientMode);
        I("maxCount", d.maxCount);
        I("particleType", d.particleType);
        I("emitMode", d.emitMode);
        I("burstCount", d.burstCount);
        I("emitShape", d.emitShape);
        F3("lineEnd", d.lineEnd);
        d.attractorLocal = j.value("attractorLocal", true);

        if (j.contains("spriteGrid") && j["spriteGrid"].is_array() && j["spriteGrid"].size() >= 2)
        {
            d.spriteGrid.x = j["spriteGrid"][0].get<uint32_t>();
            d.spriteGrid.y = j["spriteGrid"][1].get<uint32_t>();
        }

        F3("attractorPos", d.attractorPos);
        F("attractorStrength", d.attractorStrength);
        F("attractorRadius", d.attractorRadius);
        F("vortexStrength", d.vortexStrength);
        F("drag", d.drag);

        STR("name", d.name, sizeof(d.name));
        STR("texturePath", d.texturePath, sizeof(d.texturePath));
        STR("colorRampPath", d.colorRampPath, sizeof(d.colorRampPath));
    }
} // namespace

// =====================================================================
// シリアライズ
// =====================================================================
namespace ComponentMetaJson
{
    template <>
    struct ComponentSerializer<GPUParticleComponent>
    {
        // 保存は常に emitters 配列の形式で書く
        static void Save(json &j, const GPUParticleComponent &comp, SaveContext * /*ctx*/)
        {
            json arr = json::array();
            for (const auto &emitter : comp.emitters)
            {
                json ej;
                WriteDescToJson(ej, emitter.desc);
                arr.push_back(ej);
            }
            j["emitters"] = arr;
        }

        // 読み込みは2つの形式を受け付ける。
        // emitters 配列が無く、エミッタの設定がトップレベルに直接書かれている JSON は、
        // エミッタ1個のエフェクトとして読む。これでエミッタ配列を持たないプレハブも作り直さずに読める。
        static void Load(const json &j, GPUParticleComponent &comp, LoadContext * /*ctx*/)
        {
            comp.emitters.clear();

            if (j.contains("emitters") && j["emitters"].is_array())
            {
                for (const auto &ej : j["emitters"])
                {
                    ParticleEmitter emitter;
                    ReadDescFromJson(ej, emitter.desc);
                    comp.emitters.push_back(emitter);
                }
            }
            else
            {
                ParticleEmitter emitter;
                ReadDescFromJson(j, emitter.desc); // トップレベルに直接書かれた形式
                comp.emitters.push_back(emitter);
            }

            // 空配列にはしない（System 側が「最低1個ある」前提で書けるようにする）
            if (comp.emitters.empty()) comp.emitters.push_back(ParticleEmitter{});

            auto &rm = ResourceManager::Instance();
            for (auto &emitter : comp.emitters)
            {
                emitter.texture =
                    (emitter.desc.texturePath[0] != '\0') ? rm.LoadTexture(emitter.desc.texturePath) : TextureHandle{};
                emitter.colorRamp =
                    (emitter.desc.colorRampPath[0] != '\0') ? rm.LoadTexture(emitter.desc.colorRampPath) : TextureHandle{};

                emitter.isInitialized = false;
                emitter.needsGpuInit  = true;
                emitter.staticDirty   = true;
                emitter.burstTrigger  = 0.0f;
            }
        }
    };
} // namespace ComponentMetaJson

// =====================================================================
// ComponentMeta<GPUParticleComponent>
// =====================================================================
template <>
struct ComponentMeta<GPUParticleComponent>
{
    CCL_META_USING_THIS(GPUParticleComponent)

    static constexpr const char *displayName       = "GPU Particle Emitter";
    static constexpr const char *category          = CCL::Categories::Effect;
    static constexpr int         sortPriority      = 450;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    // -----------------------------------------------------------------
    // インスペクタ用の小さな部品。
    //   ラムダや匿名 namespace ではなくメンバ関数にしておくと、VS の
    //   関数ジャンプで辿れて「どこで何を描いているか」を探しやすい。
    // -----------------------------------------------------------------

    /**
     * @brief 「最小〜最大」の対を1行で編集する。
     *
     * DragFloat2 と違い、どちらが最小かが画面に出て、最小 > 最大にならないよう ImGui が抑える。
     * 呼び出し側で if (x > y) y = x; のようなクランプを書かずに済む。
     *
     * @param label 表示名。
     * @param range 編集する範囲（x=最小, y=最大）。
     * @param speed ドラッグ1px あたりの変化量。
     * @param lo    下限。
     * @param hi    上限。
     * @param fmt   数値の表示書式。
     * @return 値を変更したら true。
     */
    static bool RangeDrag(const char *label, GpuData::float2 &range,
                          float speed, float lo, float hi, const char *fmt)
    {
        return ImGui::DragFloatRange2(label, &range.x, &range.y, speed, lo, hi, fmt, fmt);
    }

    /**
     * @brief ラジアン保持の角度範囲を「度」で編集する。
     *
     * データはラジアンのまま。UI だけ度にする。「0.30 rad」と出ていても何度なのか分からないため。
     *
     * @param label   表示名。
     * @param radians 編集する角度の範囲（ラジアン。x=最小, y=最大）。
     * @param maxDeg  上限（度）。
     * @return 値を変更したら true。
     */
    static bool AngleRangeDrag(const char *label, GpuData::float2 &radians, float maxDeg)
    {
        constexpr float kRadToDeg = 57.2957795f;
        constexpr float kDegToRad = 0.01745329f;

        float lo = radians.x * kRadToDeg;
        float hi = radians.y * kRadToDeg;
        if (!ImGui::DragFloatRange2(label, &lo, &hi, 0.5f, 0.0f, maxDeg,
                                    u8"最小 %.0f°", u8"最大 %.0f°"))
            return false;

        radians.x = lo * kDegToRad;
        radians.y = hi * kDegToRad;
        return true;
    }

    // 「(?)」ヘルプを出す。ComponentMetaImGui::Help へ渡すだけの短縮名
    static void Help(const char *text) { ComponentMetaImGui::Help(text); }

    // イージング曲線の値（グラフ表示用）。particle.hlsli の ApplyEase と同じ式なので、
    //   片方を変えたらもう片方も合わせる（ずれるとグラフと実際の広がり方が食い違う）。
    static float EvalEase(float t, int mode, float power)
    {
        t             = (std::min)((std::max)(t, 0.0f), 1.0f);
        const float k = (std::max)(power, 1e-3f);
        switch (mode)
        {
        case 1: return std::pow(t, k);
        case 2: return 1.0f - std::pow(1.0f - t, k);
        case 3: return (t < 0.5f) ? 0.5f * std::pow(2.0f * t, k)
                                  : 1.0f - 0.5f * std::pow(2.0f * (1.0f - t), k);
        default: return t;
        }
    }

    static bool CustomGui(GPUParticleComponent &compRef, unsigned long long entityID = 0, void * /*worldPtr*/ = nullptr)
    {
        bool  changed = false;
        auto *p       = &compRef;
        auto &rm      = ResourceManager::Instance();
        HWND  hWnd    = Graphics::Instance().GetWindowHandle();

        // 空配列だと以下が全て破綻するので、ここで最低1個を保証する。
        if (p->emitters.empty()) p->emitters.push_back(ParticleEmitter{});

        // =============================================================
        // エフェクト全体の操作
        //   1エンティティ = 1エフェクト。ここに置くのは「エフェクト単位」の
        //   項目だけに限る。エミッタ個別の設定を混ぜると、どのエミッタに
        //   効いているのか画面から読めなくなる。
        //   「全部〜」系の操作はこの1行に集める（以前は表示系が2か所に分かれていた）。
        // =============================================================
        ImGui::SeparatorText(u8"エフェクト全体 (Effect)");

        // 選択中のエミッタ番号。Entity ごとに覚える（static だと全エフェクトで共有されてしまう）。
        //   ImGui のウィンドウ単位の保存領域に、Entity の ID を混ぜたキーで置く。
        ImGuiStorage *storage  = ImGui::GetStateStorage();
        const ImGuiID selKey   = ImGui::GetID(("ParticleSelectedEmitter" + std::to_string(entityID)).c_str());
        int           selected = storage->GetInt(selKey, 0);

        const ImGuiStyle &style   = ImGui::GetStyle();
        const float       toolBtn = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 3.0f) / 4.0f;

        // 複数エミッタを重ねた見た目は「同時に出したとき」しか確認できない。
        //   1つずつ発火しても本番と違う絵になるので、まとめて撃つ手段を用意する。
        if (ImGui::Button(u8"▶ 全部発火", ImVec2(toolBtn, 0)))
            p->TriggerBurst();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"このエフェクトの全エミッタを同じフレームで発火します。\n"
                              u8"VFX::Play() で再生したときと同じ見え方になります。");
        ImGui::SameLine();
        if (ImGui::Button(u8"↺ 全部リセット", ImVec2(toolBtn, 0)))
            for (auto &e : p->emitters)
                e.needsGpuInit = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"いま出ている粒子を全部捨てて、全エミッタを最初の状態から出し直します。");
        ImGui::SameLine();
        if (ImGui::Button(u8"全部表示", ImVec2(toolBtn, 0)))
            for (auto &e : p->emitters)
                e.previewVisible = true;
        ImGui::SameLine();
        if (ImGui::Button(u8"+ 追加", ImVec2(toolBtn, 0)))
        {
            // 一覧を描く前なので、ここで vector を増やしても参照は壊れない
            p->emitters.push_back(ParticleEmitter{});
            selected = static_cast<int>(p->emitters.size()) - 1; // 追加したものをすぐ編集できるように選ぶ
            changed  = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"このエフェクトにエミッタを追加します");

        // 構成の要約。エミッタを増やしたとき粒子の総数を見失いやすいので出す。
        int totalMax = 0, burstCount = 0, continuousCount = 0, hiddenCount = 0;
        for (const auto &e : p->emitters)
        {
            totalMax += e.desc.maxCount;
            (e.desc.emitMode == 1) ? ++burstCount : ++continuousCount;
            if (!e.previewVisible) ++hiddenCount;
        }
        ImGui::TextDisabled(u8"エミッタ %d 個 (Burst %d / 継続 %d)　粒子上限 合計 %d",
                            static_cast<int>(p->emitters.size()), burstCount, continuousCount, totalMax);

        // 継続放出が混ざっていると「全部発火」の結果が本番と一致しない。
        //   継続エミッタは発火とは無関係に出続けるので、その旨を明示する。
        if (continuousCount > 0 && burstCount > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               u8"※ 継続放出のエミッタが %d 個あります。"
                               u8"これらは発火に関係なく出続けます",
                               continuousCount);
        }

        // 非表示の層があるまま調整を続けると「なぜか薄い」の原因になる。
        //   保存されない設定なので、気付ける形で常に出しておく（戻すのは上の「全部表示」）。
        if (hiddenCount > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               u8"※ %d 個のエミッタを非表示中（プレビュー用・保存されません）", hiddenCount);
        }

        auto pickTexture = [&](char *dst, size_t cap, TextureHandle &handle, const char *title) {
            if (auto path = FileDialog::OpenPreset("Texture", title, hWnd))
            {
                strcpy_s(dst, cap, FileDialog::ConvertToRelativePath(*path).c_str());
                handle  = rm.LoadTexture(dst);
                changed = true;
            }
        };

        // =============================================================
        // エミッタ一覧（レイヤー一覧）
        //   1行 = 1エミッタ。表示・Solo・発火/リセットを各行に直接置き、
        //   どの層でも1クリックで操作できるようにする（以前はタブを選び直してから
        //   下のボタンを押す2手だった）。名前をクリックすると下で編集する対象になる。
        // =============================================================
        const int emitterCount = static_cast<int>(p->emitters.size());
        if (selected < 0 || selected >= emitterCount) selected = 0; // エミッタが減った後は範囲外になり得る

        if (ImGui::BeginTable("EmitterList", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn(u8"表示", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(u8"Solo", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(u8"名前", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(u8"操作", ImGuiTableColumnFlags_WidthFixed);

            for (int i = 0; i < emitterCount; ++i)
            {
                ParticleEmitter           &em = p->emitters[i];
                const ParticleEmitterDesc &ed = em.desc;
                ImGui::PushID(i);
                ImGui::TableNextRow();

                // ---- 表示 ----
                //   1層ずつ消して「どれがどれか」を目で確かめるための道具。
                //   保存されないので、戻し忘れてもアセットは壊れない。
                ImGui::TableNextColumn();
                ImGui::Checkbox("##visible", &em.previewVisible);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(u8"このエミッタを描画するか（プレビュー用・保存されません）");

                // ---- Solo ----
                //   この層だけ表示中なら押すと全部表示に戻る（トグル）。今 Solo 中かは色で分かるようにする。
                ImGui::TableNextColumn();
                bool isSolo = em.previewVisible && emitterCount > 1;
                for (int j = 0; j < emitterCount && isSolo; ++j)
                    if (j != i && p->emitters[j].previewVisible) isSolo = false;
                if (isSolo) ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
                if (ImGui::SmallButton("S"))
                {
                    for (int j = 0; j < emitterCount; ++j)
                        p->emitters[j].previewVisible = isSolo ? true : (j == i);
                }
                if (isSolo) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(isSolo ? u8"Solo を解除して全部表示に戻します" : u8"このエミッタだけを表示します");

                // ---- 名前（クリックで選択） ----
                //   名前が付いていれば最優先で出す（火花/残り火/フラッシュ のどれかが一目で分かる）。
                //   未命名のときだけ、モードと個数の要約で代用する。非表示中は灰色にする。
                ImGui::TableNextColumn();
                const std::string summary = (ed.emitMode == 1) ? "Burst " + std::to_string(ed.burstCount)
                                                               : u8"継続 " + std::to_string(ed.maxCount);
                const std::string caption = std::to_string(i) + ": " +
                                            ((ed.name[0] != '\0') ? std::string(ed.name) + "  (" + summary + ")" : summary);
                if (!em.previewVisible) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
                if (ImGui::Selectable(caption.c_str(), selected == i))
                    selected = i;
                if (!em.previewVisible) ImGui::PopStyleColor();

                // ---- 発火 / リセット ----
                //   バーストは「このエミッタだけ発火」、継続は発火が無いので「出し直し」を置く。
                ImGui::TableNextColumn();
                if (ed.emitMode == 1)
                {
                    if (ImGui::SmallButton(u8"▶")) em.burstTrigger = 1.0f;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"このエミッタだけ発火します（他のエミッタは動かしません）");
                }
                else
                {
                    if (ImGui::SmallButton(u8"↺")) em.needsGpuInit = true;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(u8"いま出ている粒子を捨てて、このエミッタを最初の状態から出し直します。\n"
                                          u8"継続モードは id ごとに位相をずらした状態で作り直されるので、\n"
                                          u8"出方がおかしくなったときはまずこれを試してください。");
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        auto &emitter = p->emitters[selected];
        auto &d       = emitter.desc;

        // =============================================================
        // 選択中のエミッタ
        //   「どの層を編集しているか」をここで確定させてから、下の設定タブに入る。
        // =============================================================
        ImGui::SeparatorText(u8"選択中のエミッタ (Emitter)");

        // 追加と削除は「このGUIを描き終えてから」行う。ここで vector を触ると
        //   下で参照している emitter / d が壊れる。
        bool requestErase     = false;
        bool requestDuplicate = false;

        const float dupW = ImGui::CalcTextSize(u8"複製").x + style.FramePadding.x * 2.0f;
        const float delW = ImGui::CalcTextSize(u8"削除…").x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - dupW - delW - style.ItemSpacing.x * 2.0f);
        if (ImGui::InputTextWithHint("##emitterName", u8"エミッタ名 (例: 火花 / 残り火 / フラッシュ)",
                                     d.name, IM_ARRAYSIZE(d.name)))
            changed = true;

        ImGui::SameLine();
        if (ImGui::Button(u8"複製")) requestDuplicate = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"選択中のエミッタを丸ごとコピーして末尾に追加します。\n"
                              u8"似た層をもう1枚重ねたいとき、一から設定し直さずに済みます。");

        // 削除は取り消せないので確認を挟む（以前は小さいボタンを押した瞬間に消えていた）。
        //   最後の1個は消せない（System 側が「最低1個ある」前提で書かれている）。
        ImGui::SameLine();
        ImGui::BeginDisabled(emitterCount <= 1);
        if (ImGui::Button(u8"削除…")) ImGui::OpenPopup(u8"エミッタの削除");
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal(u8"エミッタの削除", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(u8"「%d: %s」を削除しますか？", selected, (d.name[0] != '\0') ? d.name : u8"(名前なし)");
            ImGui::TextDisabled(u8"元に戻せません。");
            if (ImGui::Button(u8"削除する", ImVec2(120, 0)))
            {
                requestErase = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"キャンセル", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // 高さは中身に合わせて伸ばす。以前は 380px 固定で、インスペクタ自体のスクロールと
        //   この欄の中のスクロールが二重になっていた。
        ImGui::BeginChild("ParticleEditorChild", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NavFlattened);

        if (ImGui::BeginTabBar("ParticleTabs"))
        {
            if (ImGui::BeginTabItem(u8"基本・発生 (Base & Spawn)"))
            {
                ImGui::Spacing();
                ImGui::SeparatorText(u8"エミッタ設定 (Emitter)");
                int tempMax = d.maxCount;
                if (ImGui::DragInt(u8"最大許容量 (Max Capacity)", &tempMax, 10.0f, 1, 100000))
                {
                    if (tempMax > 0 && tempMax != d.maxCount)
                    {
                        d.maxCount            = tempMax;
                        emitter.isInitialized = false; // バッファを作り直させる
                        changed               = true;
                    }
                }
                const char *typeNames[] = {"Point (Color Only)", "Quad (Texture)"};
                if (ImGui::Combo(u8"描画タイプ (Render Type)", &d.particleType, typeNames, IM_ARRAYSIZE(typeNames))) changed = true;

                const char *modes[] = {"Continuous (継続)", "Burst (爆発)"};
                if (ImGui::Combo(u8"発生モード (Mode)", &d.emitMode, modes, IM_ARRAYSIZE(modes)))
                {
                    // Burst は burstCount 個しか出さない。0 のまま切り替えると
                    //   無言で1個も出なくなるので、既定として最大数を入れておく。
                    if (d.emitMode == 1 && d.burstCount <= 0) d.burstCount = d.maxCount;

                    // モードごとに「粒子バッファの初期状態」が別物なので必ず作り直す。
                    //   initialize_particle_cs は
                    //     Burst → 全粒子を死亡待機 (age = lifespan + 1)
                    //     継続  → age = (id / max_count) * lifespan で位相をずらす
                    //   を作る。この位相ずらしが継続放出を「定常流」にしている本体で、
                    //   切り替えただけだと前モードの状態が残り、全粒子が一斉に
                    //   生き死にする（GSが age<=0 を捨てるので、一斉に消える瞬間ができる）。
                    emitter.needsGpuInit = true;

                    // 前モードの撃ち残しを持ち込まない。継続中は burstTrigger が
                    //   消費されないので、放置すると Burst に戻した瞬間に暴発する。
                    emitter.burstTrigger = 0.0f;

                    changed = true;
                }

                // 継続モードは emit_enabled(=emitting) が閉じていると1粒も出ない。
                //   Burst 側は emit_enabled を一切見ないので、この非対称が
                //   「Burstでは出るのに継続にすると全く出ない」の正体になり得る。
                //   状態が見えないと原因を追えないので、ここに出しておく。
                if (d.emitMode == 0)
                {
                    if (emitter.emitting)
                    {
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), u8"発生ゲート: ON (放出中)");
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                           u8"発生ゲート: OFF — 継続モードでは1粒も出ません");
                        ImGui::TextDisabled(u8"　VFX::Stop() で停止された状態です。");
                    }
                }

                if (d.emitMode == 1)
                {
                    if (ImGui::DragInt(u8"発生数 (Count)", &d.burstCount, 1, 0, 10000)) changed = true;
                    Help(u8"1回の発火で出す粒子の数。\n"
                         u8"最大許容量(Max Capacity)を超える分は出ません。");
                    if (d.burstCount > d.maxCount)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                           u8"※ 発生数 %d は最大許容量 %d で頭打ちになります",
                                           d.burstCount, d.maxCount);
                    }
                }
                else
                {
                    if (RangeDrag(u8"発生間隔 (秒)", d.spawnDelay, 0.001f, 0.0f, 5.0f, "%.4f"))
                        changed = true;
                    Help(u8"次の粒子が生まれるまでの待ち時間。\n"
                         u8"この最小〜最大の間でランダムに決まります。\n"
                         u8"小さいほど密に出ます (0 = 毎フレーム)。");
                }

                ImGui::SeparatorText(u8"形状と位置 (Shape & Position)");

                const char *shapeNames[] = {u8"ディスク (Disk)", u8"線分・剣 (Line/Blade)"};
                if (ImGui::Combo(u8"発生形状 (Emit Shape)", &d.emitShape, shapeNames, IM_ARRAYSIZE(shapeNames))) changed = true;

                changed |= ImGui::DragFloat3(u8"位置オフセット (Pos Offset)", &d.localOffset.x, 0.1f);
                if (ImGui::IsItemHovered() && d.emitShape == 1)
                    ImGui::SetTooltip(u8"線分の始点(根本)。剣ボーンのローカル座標で刃の根本に合わせる。");

                if (d.emitShape == 1)
                {
                    // 線分モード: 刃先までのローカルオフセット + 太さ
                    changed |= ImGui::DragFloat3(u8"刃先オフセット (Blade Tip)", &d.lineEnd.x, 0.05f);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(u8"始点(Pos Offset)から刃先までのローカルオフセット。\n剣の刃が伸びる軸に長さぶん設定する (例: Y+ に刃長)。");
                    if (RangeDrag(u8"筒の太さ", d.emitRadius, 0.005f, 0.0f, 10.0f, "%.3f"))
                        changed = true;
                    Help(u8"刃を包む筒の半径。粒子はこの最小〜最大の間に散ります。\n"
                         u8"最小=最大 にすると刃の表面だけに張り付きます。");
                }
                else
                {
                    changed |= ImGui::DragFloat(u8"高さ範囲 Y (Height Volume)", &d.localOffsetY, 0.1f, 0.0f, 50.0f);
                    Help(u8"発生位置を上下にばらす幅。0 なら平らな円盤から出ます。");

                    if (RangeDrag(u8"半径", d.emitRadius, 0.01f, 0.0f, 100.0f, "%.2f"))
                        changed = true;
                    Help(u8"円盤の内側〜外側。最小を上げるとドーナツ状になります。");

                    changed |= ImGui::DragFloat2(u8"形状比率 XZ (Stretch)", &d.emitStretch.x, 0.01f, 0.0f, 10.0f, "%.2f");
                    Help(u8"円盤を潰して楕円にします。左=X倍率 右=Z倍率 (1,1 が真円)。");
                }

                // 角度は度で見せる。ラジアン表示だと何度なのか判断できない。
                if (AngleRangeDrag(u8"放出角度", d.coneAngle, 180.0f)) changed = true;
                Help(u8"エミッタの上方向(+Y)から何度傾けて飛ばすか。\n"
                     u8"  0° = 真上へ一直線   90° = 真横   180° = 真下\n"
                     u8"最小〜最大の間でランダムに決まります。\n"
                     u8"0°〜180° にすると全方向(球状)に飛びます。");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(u8"運動・物理 (Movement)"))
            {
                ImGui::Spacing();
                ImGui::SeparatorText(u8"速度 (Velocity)");
                if (RangeDrag(u8"初速度", d.emitSpeed, 0.1f, -100.0f, 100.0f, "%.2f")) changed = true;
                Help(u8"生まれた瞬間の速さ。最小〜最大の間でランダムに決まります。\n"
                     u8"マイナスにすると放出角度の逆向きへ飛びます。");

                if (RangeDrag(u8"回転速度", d.angularSpeed, 0.1f, -100.0f, 100.0f, "%.2f")) changed = true;
                Help(u8"粒子自身がその場で回る速さ (rad/秒)。\n"
                     u8"最小をマイナス・最大をプラスにすると左右バラバラに回ります。");

                changed |= ImGui::DragFloat(u8"重力 (Gravity)", &d.gravity, 0.1f);
                Help(u8"毎秒この量だけ Y 速度が変わります。\n"
                     u8"マイナスで落下、プラスで上昇 (煙は +0.5 程度が自然)。");

                changed |= ImGui::DragFloat(u8"最高速度 (Max Speed)", &d.maxSpeed, 0.5f, 0.0f, 1000.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(u8"粒子の速度の上限 (0 = 無制限)。\n"
                                      u8"初速をこれより大きくしても、この値まで抑えられます。");
                // 初速がクランプされて効いていない状態は気付きにくいので明示する。
                if (d.maxSpeed > 0.0f && d.emitSpeed.y > d.maxSpeed)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                       u8"※ 初速の最大 %.1f は最高速度 %.1f で頭打ちになります",
                                       d.emitSpeed.y, d.maxSpeed);
                }

                ImGui::SeparatorText(u8"乱流・ノイズ (Turbulence)");
                changed |= ImGui::DragFloat(u8"ノイズ強度 (Strength)", &d.noiseStrength, 0.1f);
                Help(u8"揺らぎの強さ。0 でノイズ計算そのものを飛ばします(軽い)。");
                // 強度0のとき Scale は絵に効かない。触れるのに何も起きないと迷うので閉じる。
                ImGui::BeginDisabled(d.noiseStrength <= 0.0f);
                changed |= ImGui::DragFloat(u8"ノイズサイズ (Scale)", &d.noiseScale, 0.01f);
                ImGui::EndDisabled();
                Help(u8"揺らぎの粒の大きさ。小さいほど細かくバラけ、\n"
                     u8"大きいほど隣同士がまとまって流れます。");
                if (d.noiseStrength <= 0.0f)
                    ImGui::TextDisabled(u8"※ ノイズ強度が 0 のためサイズは効きません");

                ImGui::SeparatorText(u8"引力・斥力 (Force Field)");
                // 強度も渦も0なら Force Field 全体が無効。まとめて閉じて意図を示す。
                const bool forceFieldOff = (d.attractorStrength == 0.0f && d.vortexStrength == 0.0f);
                changed |= ImGui::DragFloat(u8"強度 (+:引力 -:斥力)", &d.attractorStrength, 0.1f);
                Help(u8"中心へ引き寄せる力。プラスで吸い込み、マイナスで押し出します。\n"
                     u8"0 なら引力なし。");
                changed |= ImGui::DragFloat(u8"渦の強さ (Vortex)", &d.vortexStrength, 0.1f);
                Help(u8"中心軸のまわりを回す力。引力と併用すると渦を巻きます。");

                ImGui::BeginDisabled(forceFieldOff);
                changed |= ImGui::Checkbox(u8"エミッタ相対空間 (Local Space)", &d.attractorLocal);
                Help(u8"ON: 中心位置をエミッタからの相対で見る(一緒に動く)。\n"
                     u8"OFF: ワールド座標に固定する(その場に置いた渦になる)。");
                changed |= ImGui::DragFloat3(u8"中心位置 (Center)", &d.attractorPos.x, 0.05f);
                changed |= ImGui::DragFloat(u8"影響半径 (Radius)", &d.attractorRadius, 0.05f, 0.0f, 100.0f);
                Help(u8"力が届く距離。0 = 無限に届く。");
                ImGui::EndDisabled();
                if (forceFieldOff)
                    ImGui::TextDisabled(u8"※ 強度・渦がどちらも 0 のため引力は無効です");

                changed |= ImGui::DragFloat(u8"空気抵抗・減衰 (Drag)", &d.drag, 0.05f, 0.0f, 20.0f);
                Help(u8"速度を毎秒この割合だけ削ります。大きいほど早く止まります。");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(u8"サイズ・寿命 (Size & Life)"))
            {
                ImGui::Spacing();
                ImGui::SeparatorText(u8"寿命 (Lifespan)");
                if (RangeDrag(u8"生存時間 (秒)", d.lifespan, 0.05f, 0.01f, 60.0f, "%.2f")) changed = true;
                Help(u8"粒子が消えるまでの時間。最小〜最大の間でランダムに決まります。\n"
                     u8"Burst の場合、VFXSystem はこの最大値を見て後始末の時刻を決めます。");

                ImGui::SeparatorText(u8"サイズ (Size)");
                // ここは min/max ではなく「誕生時 → 消滅時」。RangeDrag を使うと
                //   大きく生まれて小さく消える設定 (start > end) が作れなくなるので
                //   意図して DragFloat2 のまま残す。
                changed |= ImGui::DragFloat2(u8"サイズ 誕生→消滅", &d.emitSize.x, 0.01f, 0.0f, 100.0f, "%.2f");
                Help(u8"左=生まれた瞬間のサイズ  右=消える瞬間のサイズ。\n"
                     u8"左>右 で縮みながら消え、左<右 で膨らみながら消えます。\n"
                     u8"衝撃波は 左を小さく・右を大きく してください。");

                const char *easeModes[] = {u8"線形 (Linear)", u8"Ease-In (加速)", u8"Ease-Out (減速)", u8"Ease-InOut"};
                if (ImGui::Combo(u8"サイズの変化 (Easing)", &d.sizeEase, easeModes, IM_ARRAYSIZE(easeModes))) changed = true;
                Help(u8"誕生→消滅のサイズをどんな速さで変えるか。\n"
                     u8"Ease-Out = 最初に一気に広がって最後はゆっくり（衝撃波の定番）。\n"
                     u8"Ease-In  = ゆっくり始まって最後に加速。");

                // 線形のときは強さが効かないので触れないようにする
                ImGui::BeginDisabled(d.sizeEase == 0);
                changed |= ImGui::SliderFloat(u8"変化の強さ (Power)", &d.sizeEasePower, 1.0f, 8.0f, "%.1f");
                ImGui::EndDisabled();
                Help(u8"曲線の急さ。1 で線形と同じ、2 で緩やか、3〜5 で衝撃波らしい急な広がり。");

                // 曲線のグラフ。横 = 寿命の経過（左が誕生、右が消滅）、縦 = 誕生→消滅のどこまで進んだか
                {
                    float curve[48];
                    for (int i = 0; i < IM_ARRAYSIZE(curve); ++i)
                        curve[i] = EvalEase(i / float(IM_ARRAYSIZE(curve) - 1), d.sizeEase, d.sizeEasePower);
                    ImGui::PlotLines("##sizeEaseCurve", curve, IM_ARRAYSIZE(curve), 0,
                                     u8"誕生 → 消滅", 0.0f, 1.0f, ImVec2(-1, 60));
                }

                changed |= ImGui::SliderFloat(u8"サイズのばらつき", &d.sizeRandom, 0.0f, 1.0f, "%.2f");
                Help(u8"0 = 上のサイズちょうどで固定（毎回まったく同じ大きさ）\n"
                     u8"1 = 0〜上のサイズ の一様ランダム\n"
                     u8"煙や火花は 1 が自然ですが、衝撃波のように「1枚を決まった\n"
                     u8"大きさに広げる」効果は 0 にしないと毎回大きさが変わります。");

                // 少数しか出さない効果では、ばらつきがそのまま「毎回違う見た目」になる。
                //   粒子が多ければ平均化されて気付かないので、少ないときだけ助言する。
                const int spawnedCount = (d.emitMode == 1) ? d.burstCount : d.maxCount;
                if (d.sizeRandom > 0.0f && spawnedCount > 0 && spawnedCount <= 8)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                       u8"※ %d 個しか出さない設定です。ばらつきが毎回そのまま\n"
                                       u8"   見た目の差になります。固定したいなら 0 にしてください",
                                       spawnedCount);
                }

                changed |= ImGui::DragFloat2(u8"サイズ倍率 誕生/消滅", &d.scale.x, 0.01f, 0.0f, 100.0f, "%.2f");
                Help(u8"上のサイズに掛ける倍率。全体を一括で大きく/小さくするときに使います。\n"
                     u8"左が誕生時、右が消滅時に掛かります (1,1 で等倍)。");

                ImGui::SeparatorText(u8"フェード (Fade)");
                changed |= ImGui::DragFloat2(u8"フェード 入り/明け (秒)", &d.fadeDuration.x, 0.01f, 0.0f, 10.0f, "%.2f");
                Help(u8"左=生まれてから不透明になるまでの秒数(フェードイン)\n"
                     u8"右=消える前に薄くなり始める秒数(フェードアウト)。\n"
                     u8"合計が生存時間より長いと、完全に不透明になりません。");
                // 合計が寿命を超えていると「なぜか薄いまま」になる。原因が見えないので明示する。
                if (d.fadeDuration.x + d.fadeDuration.y > d.lifespan.x)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                       u8"※ フェード合計 %.2f 秒 > 生存時間の最小 %.2f 秒。"
                                       u8"短命な粒子は不透明になりません",
                                       d.fadeDuration.x + d.fadeDuration.y, d.lifespan.x);
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(u8"描画・テクスチャ (Graphics)"))
            {
                ImGui::Spacing();
                // 並びは「見た目が決まる順」。形（テクスチャ）→ 色 → 向き → 細かい効果。
                //   一番よく触るテクスチャを最下部に置いていたので、先頭へ移した。
                ImGui::SeparatorText(u8"テクスチャ (Texture)");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
                if (ImGui::InputText("##texPath", d.texturePath, sizeof(d.texturePath))) changed = true;
                ImGui::SameLine();
                if (ImGui::Button(u8"読込##Main")) pickTexture(d.texturePath, sizeof(d.texturePath), emitter.texture, "Select Texture");

                if (ID3D11ShaderResourceView *srv = rm.GetTexture(emitter.texture))
                {
                    ImGui::Image((void *)srv, ImVec2(64, 64), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
                    ImGui::SameLine();
                    if (ImGui::Button(u8"クリア##Main"))
                    {
                        memset(d.texturePath, 0, sizeof(d.texturePath));
                        emitter.texture = TextureHandle{};
                        changed         = true;
                    }
                }

                ImGui::SeparatorText(u8"カラー・ブレンド (Color & Blend)");
                const char *renderModes[] = {"Additive (Fire/Light)", "Alpha Blend (Smoke/Dark)"};
                if (ImGui::Combo(u8"描画ブレンド (Render Mode)", &d.renderMode, renderModes, IM_ARRAYSIZE(renderModes))) changed = true;

                const char *colorModes[] = {u8"単色 (Single Color)", u8"カラーランプ (Color Ramp)"};
                if (ImGui::Combo(u8"カラーモード (Color Mode)", &d.colorMode, colorModes, IM_ARRAYSIZE(colorModes))) changed = true;

                if (d.colorMode == 0)
                {
                    changed |= ImGui::ColorEdit4(u8"基本色 (Base Color)", &d.color.x);
                }
                else
                {
                    // カラーランプはランプモードでしか使わないので、選んだときだけここに出す
                    //   （以前はタブ最下部に常に出ていて、どのモードで効くのか読めなかった）。
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
                    if (ImGui::InputText("##rampPath", d.colorRampPath, sizeof(d.colorRampPath))) changed = true;
                    ImGui::SameLine();
                    if (ImGui::Button(u8"読込##Ramp")) pickTexture(d.colorRampPath, sizeof(d.colorRampPath), emitter.colorRamp, "Select Color Ramp");

                    if (ID3D11ShaderResourceView *srv = rm.GetTexture(emitter.colorRamp))
                    {
                        ImGui::Image((void *)srv, ImVec2(128, 16), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
                        ImGui::SameLine();
                        if (ImGui::Button(u8"クリア##Ramp"))
                        {
                            memset(d.colorRampPath, 0, sizeof(d.colorRampPath));
                            emitter.colorRamp = TextureHandle{};
                            changed           = true;
                        }
                    }
                    else
                    {
                        // ランプが無いとシェーダーは黒を読み、加算では何も見えなくなる（エラーも出ない）
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                           u8"※ カラーランプが未設定です。粒子が黒くなり、加算では見えません");
                    }
                }

                changed |= ImGui::DragFloat(u8"発光強度 (Intensity)", &d.intensity, 0.05f, 0.0f, 50.0f, "%.2f");
                Help(u8"色に掛ける倍率。1 で選んだ色そのまま。\n"
                     u8"1 を超えると光ってブルームがかかる（目立たせたいなら 2〜5 程度）。\n"
                     u8"0 で真っ黒（加算では見えなくなる）。");

                ImGui::SeparatorText(u8"向き (Orientation)");
                const char *orientModes[] = {u8"ビルボード (カメラを向く)", u8"ワールド水平 (地面と平行)", u8"エミッタの向きの平面"};
                if (ImGui::Combo(u8"向き (Orient Mode)", &d.orientMode, orientModes, IM_ARRAYSIZE(orientModes))) changed = true;
                Help(u8"ビルボード: 常にカメラを向く（火花・煙など）。\n"
                     u8"ワールド水平: 地面と平行に寝る（2段ジャンプの衝撃波リングなど）。\n"
                     u8"エミッタの向きの平面: 水平面をエミッタの回転で傾ける（壁の波紋など）。\n"
                     u8"平面の2つは大きさがそのままメートル（中心から端まで）になる。");

                ImGui::SeparatorText(u8"特殊効果 (Effects)");
                // 速度ストレッチは画面上で計算するので、平面モードでは効かない。
                //   無言で効かないより、触れないようにして理由を出す（UV スクロールと同じ）。
                const bool isFlat = (d.orientMode != 0);
                ImGui::BeginDisabled(isFlat);
                changed |= ImGui::DragFloat(u8"速度ストレッチ (Velocity Stretch)", &d.velocityStretch, 0.01f, 0.0f, 10.0f);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"移動方向に引き伸ばします (0.0=無効)");
                if (isFlat)
                    ImGui::TextDisabled(u8"※ 平面モードでは速度ストレッチは無効です");
                // スプライトシート使用中は UV スクロールが使えない（コマの外へ滲むため）。
                //   無言で壊れるより、触れないようにして理由を出す。
                const bool usesSpriteSheet = (d.spriteGrid.x * d.spriteGrid.y > 1);
                ImGui::BeginDisabled(usesSpriteSheet);
                changed |= ImGui::DragFloat2(u8"UVスクロール (UV Scroll Speed)", &d.uvScrollSpeed.x, 0.01f);
                ImGui::EndDisabled();
                if (usesSpriteSheet)
                    ImGui::TextDisabled(u8"※ スプライトシート使用中は UV スクロールは無効です");

                ImGui::SeparatorText(u8"スプライトアニメ (Sprite Sheet)");
                int grid[2] = {(int)d.spriteGrid.x, (int)d.spriteGrid.y};
                if (ImGui::DragInt2(u8"分割数 (Grid X/Y)", grid, 0.1f, 1, 16))
                {
                    d.spriteGrid.x = uint32_t(grid[0] < 1 ? 1 : grid[0]);
                    d.spriteGrid.y = uint32_t(grid[1] < 1 ? 1 : grid[1]);
                    changed        = true;
                }
                const char *animModes[] = {"Random (Fixed)", "Life Cycle (Animate)"};
                if (ImGui::Combo(u8"再生モード (Anim Mode)", &d.spriteMode, animModes, IM_ARRAYSIZE(animModes))) changed = true;

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::EndChild();

        // エミッタ削除はここで実行する。GUIを描き終えた後なので、
        //   上で握っていた emitter / d の参照が壊れる心配がない。
        if (requestDuplicate)
        {
            // desc だけを引き継ぎ、GPU側の実体（バッファ・ハンドル・乱数種）は
            //   引き継がない。ハンドルを複製すると、同じバッファを2つのエミッタが
            //   指してしまい、片方を消したときにもう片方が壊れる。
            ParticleEmitter copy;
            copy.desc = p->emitters[selected].desc;

            // 名前の重複を避けて " copy" を足す。ただし入りきる時だけ。
            //   strcat_s はバッファ不足だと不正パラメータハンドラを呼んで落ちるので、
            //   長い名前を付けられていても安全なように、事前に空きを確かめる。
            const char  *suffix = " copy";
            const size_t used   = strnlen_s(copy.desc.name, sizeof(copy.desc.name));
            if (used + strlen(suffix) < sizeof(copy.desc.name))
                strcat_s(copy.desc.name, sizeof(copy.desc.name), suffix);

            p->emitters.push_back(copy);
            selected = static_cast<int>(p->emitters.size()) - 1; // 複製したものをすぐ編集できるように選ぶ
            changed  = true;
        }

        if (requestErase)
        {
            // GPUリソースを取りこぼさないよう、消す前に解放しておく。
            if (p->emitters[selected].computeBuffer.IsValid())
                rm.UnloadParticleBuffer(p->emitters[selected].computeBuffer);

            p->emitters.erase(p->emitters.begin() + selected);
            if (selected >= static_cast<int>(p->emitters.size())) selected = static_cast<int>(p->emitters.size()) - 1;
            changed = true;
        }

        storage->SetInt(selKey, selected);

        if (changed) p->MarkDescChanged();

        return changed;
    }
#else
    static bool CustomGui(GPUParticleComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(GPUParticleComponent, "GPUParticle")
REGISTER_COMPONENT_EDITOR_GUI(GPUParticleComponent)