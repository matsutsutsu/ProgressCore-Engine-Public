#include "Shader.h"
#include "Engine/Graphics/Resource/ResourceManager.h" 
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <algorithm>

bool Shader::InitializeReflection(ID3D11Device* device, const std::string& csoPath)
{
	// csoファイルからリフレクション情報をロードし、定数バッファの自動生成を行う
    if (!_reflection.LoadFromCompiledFile(csoPath.c_str())) return false;

	// 定数バッファの取得
    const auto& cbs = _reflection.GetConstantBuffers();
    for (const auto& cbInfo : cbs)
    {
		// SLOT_CB_SCENE(slot8)以降はシステム予約領域なので、
        // ユーザー定数バッファのみを自動生成する
        if (cbInfo.bindPoint >= SLOT_CB_SCENE) {
            continue; // システム予約領域は無視
        }
		// AutoConstantBuffer を生成してマップに保存
        _constantBuffers[cbInfo.name].Create(device, cbInfo);
    }
    return true;
}

int Shader::FetchTextureSlot(const std::string& name)
{
    int slot = _reflection.FindTextureSlot(name);
    if (slot != -1) {
        _textureSlots[name] = slot;
        _usedTextureSlots.push_back(slot);
    }
    return slot;
}

int Shader::FetchSamplerSlot(const std::string& name)
{
    int slot = _reflection.FindSamplerSlot(name);
    if (slot != -1) {
        _samplerSlots[name] = slot;
    }
    return slot;
}

void Shader::ApplyShaderPipeline(ID3D11DeviceContext* dc, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11InputLayout* layout)
{
	// シェーダーと入力レイアウトをセット
    dc->IASetInputLayout(layout);
    dc->VSSetShader(vs, nullptr, 0);
    dc->PSSetShader(ps, nullptr, 0);

	// 定数バッファをバインド
    for (auto& pair : _constantBuffers) {
        pair.second.BindVS(dc);
        pair.second.BindPS(dc);
    }
}

void Shader::UnbindResources(ID3D11DeviceContext* dc)
{
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11Buffer* nullCB = nullptr;

    for (auto& pair : _constantBuffers) {
        UINT slot = (UINT)pair.second.GetBindPoint();
        dc->VSSetConstantBuffers(slot, 1, &nullCB);
        dc->PSSetConstantBuffers(slot, 1, &nullCB);
    }

    for (int slot : _usedTextureSlots) {
        dc->PSSetShaderResources((UINT)slot, 1, &nullSRV);
    }

    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->IASetInputLayout(nullptr);
}

// マテリアルのプロパティを定数バッファに転送し、テクスチャをバインドする
void Shader::UpdateMaterialProperties(ID3D11DeviceContext* dc, const MaterialData& mat)
{
	// 定数バッファにマテリアルのスカラー値とカラー値を流し込む
    for (auto& cbPair : _constantBuffers) {
        auto& cb = cbPair.second;
        bool isUpdated = false;

        // ========================================================
        // ① 定数バッファへの値の流し込み（ループの修正）
        // ========================================================

        for (const auto& prop : mat.scalars) {
           
            if (cb.SetValue(prop.nameHash, prop.value)) isUpdated = true;
        }

        for (const auto& prop : mat.colors) {
            if (cb.SetValue(prop.nameHash, prop.value)) isUpdated = true;
        }

        if (isUpdated) {
            cb.UpdateBuffer(dc);
        }
    }

    // ========================================================
    // ② テクスチャのバインド
    // ========================================================
    const auto& shaderTextures = _reflection.GetTextures();

    for (const auto& texInfo : shaderTextures) {
		// SLOT_SRV_FOG_NOISE(slot9)以降はシステム予約領域なので、
        // ユーザー定義テクスチャのみを処理する
        if (texInfo.bindPoint >= SLOT_SRV_FOG_NOISE) continue;
        // t4/t5 は SceneColorGrabPass 等がパイプライン側でバインドする per-pass リソース。
        // マテリアルに無いから null を刺すと屈折等が常に黒になる。
        if (texInfo.bindPoint == SLOT_SRV_SCENE_DEPTH ||
            texInfo.bindPoint == SLOT_SRV_SCENE_COLOR ||
            texInfo.bindPoint == SLOT_SRV_SCENE_NORMAL) {
            continue;
        }

		// テクスチャのスロット番号を取得　レジスタ番号
        UINT slot = texInfo.bindPoint;
        TextureHandle texHandle;

        // ⭕ 究極の最適化: 実行時のハッシュ計算すら不要。ロード時に計算済みの texInfo.nameHash を使う！
        if (mat.GetTexture(texInfo.nameHash, texHandle) && texHandle.IsValid()) {
            // ★cppに分けたことで、ここでResourceManagerを安全に呼び出せる
            ID3D11ShaderResourceView* srv = ResourceManager::Instance().GetTexture(texHandle);
            dc->PSSetShaderResources(slot, 1, &srv);

            if (std::find(_usedTextureSlots.begin(), _usedTextureSlots.end(), slot) == _usedTextureSlots.end()) {
                _usedTextureSlots.push_back(slot);
            }
        }
        else {
            ID3D11ShaderResourceView* nullSRV = nullptr;
            dc->PSSetShaderResources(slot, 1, &nullSRV);
        }
    }
}

std::filesystem::file_time_type Shader::GetLastWriteTime(const std::wstring& filepath)
{
    if (std::filesystem::exists(filepath)) {
        return std::filesystem::last_write_time(filepath);
    }
    return (std::filesystem::file_time_type::min)();
}

void Shader::ResetReflection()
{
    _constantBuffers.clear();
    _textureSlots.clear();
    _samplerSlots.clear();
    _usedTextureSlots.clear();
    _reflection = ShaderReflect::ReflectionData();
}
