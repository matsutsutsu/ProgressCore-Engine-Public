#pragma once
#include <imgui.h>
#include <string>
#include "Engine/Graphics/Resource/MaterialData.h" // データ定義のみインクルード

class MaterialEditor
{
public:
    // MaterialComponentではなく、MaterialDataのポインタを直接受け取る
    // これにより、ECSコンポーネントも、リソース原本も編集可能になる
    static void Draw(MaterialData* mat)
    {
        // ポインタがnullなら描画しない
        if (!mat) return;

        // ImGuiのID衝突防止（メモリアドレスをIDにする）
        ImGui::PushID(mat);

        // 1. シェーダーの切り替え
        // 修正: mat.data-> ではなく mat-> でアクセス
        int currentShader = static_cast<int>(mat->shaderId);

        // ※項目の並び順は ShaderId の定義順と一致させること
        if (ImGui::Combo("Shader", &currentShader, "Basic\0Lambert\0Phong\0Toon\0ShadowMap\0Outline\0\0"))
        {
            mat->shaderId = static_cast<ShaderId>(currentShader);
        }

        ImGui::Separator();

        // 2. スカラー値 (float) の自動生成
        if (!mat->scalars.empty())
        {
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 1.0f, 1.0f), "[ Scalars ]");
            for (auto& [key, value] : mat->scalars)
            {
                // ドラッグで数値を変更
                ImGui::DragFloat(key.c_str(), &value, 0.01f);
            }
            ImGui::Spacing();
        }

        // 3. カラー値 (float4) の自動生成
        if (!mat->colors.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 1.0f, 1.0f), "[ Colors ]");
            for (auto& [key, value] : mat->colors)
            {
                // カラーピッカーを表示
                ImGui::ColorEdit4(key.c_str(), &value.x);
            }
            ImGui::Spacing();
        }

        // 4. テクスチャ情報の表示
        if (!mat->textures.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "[ Textures ]");
            for (auto &[key, handle] : mat->textures) {
                ImGui::Text("%s:", key.c_str());
                ImGui::SameLine();

                // ★修正1: ハンドルの有効性チェックは .IsValid() を使う
                // 変更前: ImGui::TextDisabled(ptr ? "Bound" : "Null");
                ImGui::TextDisabled(handle.IsValid() ? "Bound" : "Null");

                // サムネイル表示
                // ★修正2: if (ptr) ではなく IsValid()
                if (handle.IsValid()) {
                    // ★修正3: .Get() ではなく ResourceManager経由でポインタを取得
                    // 変更前: ImGui::Image((ImTextureID)ptr.Get(), ImVec2(64, 64));
                    ID3D11ShaderResourceView *srv = ResourceManager::Instance().GetTexture(handle);
                    if (srv) {
                        ImGui::Image((ImTextureID)srv, ImVec2(64, 64));
                    }
                }
            }
        }

        // 5. アルファ設定（共通設定）
        ImGui::Separator();
        int alphaMode = static_cast<int>(mat->alphaMode);
        if (ImGui::Combo("Alpha Mode", &alphaMode, "Opaque\0Mask\0Blend\0\0")) {
            mat->alphaMode = static_cast<AlphaMode>(alphaMode);
        }

        if (mat->alphaMode == AlphaMode::Mask) {
            ImGui::SliderFloat("Alpha Cutoff", &mat->alphaCutoff, 0.0f, 1.0f);
        }

        ImGui::PopID();
    }
};