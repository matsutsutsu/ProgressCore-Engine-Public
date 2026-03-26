#pragma once

#include "Engine/Graphics/Core/RenderContext.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include <imgui.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include "Engine/Graphics/Shader/Pass/ShadowMap.h" // ShadowMapの名前取得に必要
#include "Engine/Graphics/Core/Light.h"

#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Resource/MaterialData.h"



class Shader
{
public:
	Shader() {}
	virtual ~Shader() {}

	virtual void Begin(const RenderContext& rc) = 0;
	virtual void Update(const RenderContext& rc, const Model::Mesh& mesh) = 0;
    // 終了処理を virtual で定義しつつ、共通の解除処理を呼ぶようにする
    virtual void End(const RenderContext& rc) = 0;
	//virtual void DrawGui() {}

    // === リフレクション情報へのアクセス ===
    const ShaderReflect::ReflectionData& GetReflection() const { return _reflection; }

    // 定数バッファへのアクセス（追加）
    const std::map<std::string, ShaderReflect::AutoConstantBuffer>& GetConstantBuffers() const { return _constantBuffers; }

    // 定数バッファに値を設定（外部から使用可能）
    template<typename T>
    bool SetConstantValue(const std::string& bufferName, const std::string& varName, const T& value)
    {
        auto it = _constantBuffers.find(bufferName);
        if (it == _constantBuffers.end()) return false;
        return it->second.SetValue(varName, value);
    }

    // ホットリロードのためのインターフェース
    virtual void CheckAndReload(ID3D11Device *device) {}

protected:
    // --- 共通機能：リフレクションデータ ---
    ShaderReflect::ReflectionData _reflection;

    // 定数バッファのマップ（複数のバッファを持つシェーダーに対応）
    std::map<std::string, ShaderReflect::AutoConstantBuffer> _constantBuffers;

    // よく使うスロット番号をキャッシュするマップ（使用頻度は減りますが、互換性のため残します）
    std::map<std::string, int> _textureSlots;
    std::map<std::string, int> _samplerSlots;

    // 解除対象のテクスチャスロット一覧
    std::vector<int> _usedTextureSlots;


    // 共通の初期化処理（派生クラスのコンストラクタで呼ぶ）
    bool InitializeReflection(ID3D11Device* device, const std::string& csoPath) {
        if (!_reflection.LoadFromCompiledFile(csoPath.c_str())) return false;

        // リフレクションで見つかった全ての定数バッファを確認
        const auto& cbs = _reflection.GetConstantBuffers();
        for (const auto& cbInfo : cbs)
        {
            // 【判定ロジック】
            // システム予約領域 (b8以降: Scene, Light, Shadow, Fog, Skeleton...) は
            // ModelRendererが一括管理しているので、個別のShaderクラスでは作成しない(無視する)。
            if (cbInfo.bindPoint >= SLOT_CB_SCENE) // >= 8
            {
                // 無視する
                continue;
            }

            // マテリアル用などのローカル領域 (b0 ~ b7) なら自動生成して管理する
            _constantBuffers[cbInfo.name].Create(device, cbInfo);
        }
        return true;
    }

    // --- スロット取得（自動登録機能付き） ---
    int FetchTextureSlot(const std::string& name) {
        int slot = _reflection.FindTextureSlot(name);
        if (slot != -1) {
            _textureSlots[name] = slot;
            // 【重要】解除対象としてリストに追加
            _usedTextureSlots.push_back(slot);
        }
        return slot;
    }

    int FetchSamplerSlot(const std::string& name) {
        int slot = _reflection.FindSamplerSlot(name);
        if (slot != -1) {
            _samplerSlots[name] = slot;
        }
        return slot;
    }



    // バインド用関数
    void ApplyShaderPipeline(ID3D11DeviceContext* dc, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11InputLayout* layout) {
        dc->IASetInputLayout(layout);
        dc->VSSetShader(vs, nullptr, 0);
        dc->PSSetShader(ps, nullptr, 0);

        // 【ここが重要】リフレクションで管理している全CBを自動バインド
        // どのバッファが VS 用か PS 用か厳密に分けるのは大変なので、
        // 全てのバッファを両方にバインドするのが最も安全で汎用的です。
        for (auto& pair : _constantBuffers) {
            pair.second.BindVS(dc);
            pair.second.BindPS(dc);
        }
    }

    // Slotのスロット解除
    void UnbindResources(ID3D11DeviceContext* dc) {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11Buffer* nullCB = nullptr;

        // 1. 全ての定数バッファを自動解除（リフレクションの力を借りる）
        for (auto& pair : _constantBuffers) {
            UINT slot = (UINT)pair.second.GetBindPoint();
            dc->VSSetConstantBuffers(slot, 1, &nullCB);
            dc->PSSetConstantBuffers(slot, 1, &nullCB);
        }

        // 2. テクスチャスロットの解除 (FetchTextureSlot で登録したものをすべて掃除)
        for (int slot : _usedTextureSlots) {
            dc->PSSetShaderResources((UINT)slot, 1, &nullSRV);
        }

        dc->VSSetShader(nullptr, nullptr, 0);
        dc->PSSetShader(nullptr, nullptr, 0);
        dc->IASetInputLayout(nullptr);
    }

    // シェーダーはコンポーネント（入れ物）ではなく、データ（中身）だけを知っていれば良い
    void UpdateMaterialProperties(ID3D11DeviceContext* dc, const MaterialData& mat) 
    {
        // 1. 全ての定数バッファを走査
        for (auto& cbPair : _constantBuffers) {
            auto& cb = cbPair.second;
            bool isUpdated = false;

            // 2. マテリアル内のスカラー（float）を適用
            // mat.data->scalars ではなく matData.scalars に
            for (const auto& prop : mat.scalars) {
                if (cb.SetValue(prop.first, prop.second)) isUpdated = true;
            }

            // 3. マテリアル内のカラー（Vector4）を適用
            for (const auto& prop : mat.colors) {
                if (cb.SetValue(prop.first, prop.second)) isUpdated = true;
            }

            // 値が一つでも更新されていればGPUへ転送
            if (isUpdated) {
                cb.UpdateBuffer(dc);
            }
        }

        // 2. テクスチャの更新
        // シェーダーが要求しているテクスチャ一覧を取得
        const auto& shaderTextures = _reflection.GetTextures();

        for (const auto& texInfo : shaderTextures) {

            // グローバルスロット（ShadowMap:t10など）はマテリアルで上書きしない！
            // ここで null をセットしてしまうと、せっかくModelRendererで貼った影が消えます。
            if (texInfo.bindPoint >= SLOT_SRV_SHADOW) // >= 10
            {
                continue;
            }
            if (texInfo.bindPoint >= SLOT_SRV_FOG_NOISE) // >= 9
            {
                continue;
            }

            // シェーダーが要求する名前のテクスチャを、マテリアルが持っているか確認
            auto it = mat.textures.find(texInfo.name);
            UINT slot = texInfo.bindPoint;

            if (it != mat.textures.end() && it->second.IsValid()) {

                // 管理人からテクスチャの実体(ポインタ)をもらう
                ID3D11ShaderResourceView *srv = ResourceManager::Instance().GetTexture(it->second);

                // 持っている場合：バインドする
                dc->PSSetShaderResources(slot, 1, &srv);

                // 終了時の解除用リストに追加
                if (std::find(_usedTextureSlots.begin(), _usedTextureSlots.end(), slot) == _usedTextureSlots.end()) {
                    _usedTextureSlots.push_back(slot);
                }
            }
            else {
                // 持っていない場合：明示的に null をバインドして、前の描画の影響を消す
                ID3D11ShaderResourceView* nullSRV = nullptr;
                dc->PSSetShaderResources(slot, 1, &nullSRV);
            }
        }
    }


    // 最後にファイルを読み込んだ時間
    std::filesystem::file_time_type _lastWriteTime;

    // ヘルパー関数: ファイルの最終更新日時を安全に取得
    std::filesystem::file_time_type GetLastWriteTime(const std::string &filepath)
    {
        if (std::filesystem::exists(filepath)) {
            return std::filesystem::last_write_time(filepath);
        }
        // min をカッコで囲むことで、Windowsマクロの魔の手から逃れる
        return (std::filesystem::file_time_type::min)();
    }

};
