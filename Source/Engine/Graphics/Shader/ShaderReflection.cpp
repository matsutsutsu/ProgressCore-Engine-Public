#include "ShaderReflection.h"
#include "Engine/Platform/Core/Logger.h"
#include <fstream>

namespace ShaderReflect
{
    // =======================================================
    // ハッシュ値による検索の実装
    // =======================================================
    const VariableInfo *ConstantBufferInfo::FindVariable(uint32_t nameHash) const
    {
        for (const auto &var : variables)
        {
            if (var.nameHash == nameHash)
            {
                return &var;
            }
        }
        return nullptr;
    }

    // (既存) 文字列による検索の実装
    const VariableInfo *ConstantBufferInfo::FindVariable(const std::string &name) const
    {
        for (const auto &var : variables)
        {
            if (var.name == name)
            {
                return &var;
            }
        }
        return nullptr;
    }

    // .csoファイルから読み込み
    bool ReflectionData::LoadFromCompiledFile(const char *filename)
    {
        // ファイルを読み込む
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            CCL_LOG_ERROR(LogCategory::Render, "Failed to open compiled shader file: %s", filename);
            return false;
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<char> bytecode(fileSize);
        file.read(bytecode.data(), fileSize);
        file.close();

        return LoadFromBytecode(bytecode.data(), bytecode.size());
    }

    // バイトコードから読み込み
    bool ReflectionData::LoadFromBytecode(const void *bytecode, size_t bytecodeSize)
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;

        // D3DReflectで読み込んだ.csoファイルを解析（リフレクション）する
        // これでシェーダーが何を要求しているか全情報を取得できる
        HRESULT hr = D3DReflect(
            bytecode,
            bytecodeSize,
            IID_ID3D11ShaderReflection,
            (void **)reflection.GetAddressOf());

        if (FAILED(hr))
        {
            CCL_LOG_ERROR(LogCategory::Render, "Failed to create shader reflection");
            return false;
        }

        // この関数の中でその情報を取り出す
        ExtractReflectionInfo(reflection.Get());
        return true;
    }

    // 既存情報を保持しつつ、別ステージのリフレクションを名前単位で合成する
    void ReflectionData::MergeFromBytecode(const void *bytecode, size_t bytecodeSize)
    {
        // いったん一時オブジェクトへ抽出してから、名前で重複排除しつつ取り込む。
        // （LoadFromBytecode は内部で clear するため、直接呼ぶと上書きになってしまう）
        ReflectionData tmp;
        if (!tmp.LoadFromBytecode(bytecode, bytecodeSize)) return;

        auto mergeByName = [](auto &dst, const auto &src) {
            for (const auto &s : src)
            {
                bool exists = false;
                for (const auto &d : dst)
                {
                    if (d.name == s.name)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists) dst.push_back(s);
            }
        };

        mergeByName(constantBuffers, tmp.constantBuffers);
        mergeByName(textures, tmp.textures);
        mergeByName(samplers, tmp.samplers);
        mergeByName(uavs, tmp.uavs);
    }

    // リフレクション情報を抽出
    void ReflectionData::ExtractReflectionInfo(ID3D11ShaderReflection *reflection)
    {
        D3D11_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

        // === 定数バッファの抽出 ===
        constantBuffers.clear();
        for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
        {
            ID3D11ShaderReflectionConstantBuffer *cb = reflection->GetConstantBufferByIndex(i);
            D3D11_SHADER_BUFFER_DESC              bufferDesc;
            cb->GetDesc(&bufferDesc);

            ConstantBufferInfo cbInfo;
            cbInfo.name = bufferDesc.Name;
            cbInfo.size = bufferDesc.Size;

            // バインドポイントを取得
            for (UINT j = 0; j < shaderDesc.BoundResources; ++j)
            {
                D3D11_SHADER_INPUT_BIND_DESC bindDesc;
                reflection->GetResourceBindingDesc(j, &bindDesc);

                if (bindDesc.Type == D3D_SIT_CBUFFER &&
                    strcmp(bindDesc.Name, bufferDesc.Name) == 0)
                {
                    // レジスタ番号（スロット）が入る
                    cbInfo.bindPoint = bindDesc.BindPoint;
                    break;
                }
            }

            // 変数情報を取得
            for (UINT j = 0; j < bufferDesc.Variables; ++j)
            {
                ID3D11ShaderReflectionVariable *var = cb->GetVariableByIndex(j);
                D3D11_SHADER_VARIABLE_DESC      varDesc;
                var->GetDesc(&varDesc);

                ID3D11ShaderReflectionType *type = var->GetType();
                D3D11_SHADER_TYPE_DESC      typeDesc;
                type->GetDesc(&typeDesc);

                VariableInfo varInfo;
                varInfo.name     = varDesc.Name;
                varInfo.nameHash = CCL::Utils::HashString(varDesc.Name);
                varInfo.offset   = varDesc.StartOffset;
                varInfo.size     = varDesc.Size;
                varInfo.type     = typeDesc.Type;
                varInfo.rows     = typeDesc.Rows;
                varInfo.columns  = typeDesc.Columns;

                cbInfo.variables.push_back(varInfo);
            }

            constantBuffers.push_back(cbInfo);
        }

        // === UAVの抽出 ===
        uavs.clear();
        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc;
            reflection->GetResourceBindingDesc(i, &bindDesc);

            // UAV (RWStructuredBuffer または RWTexture) かどうか判定
            if (bindDesc.Type == D3D_SIT_UAV_RWTYPED || bindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_RWBYTEADDRESS)
            {
                UnorderedAccessViewInfo uavInfo;
                uavInfo.name      = bindDesc.Name;
                uavInfo.bindPoint = bindDesc.BindPoint;
                // サイズ等は必要なら bindDesc.NumSamples
                // などから推測しますが、基本はBindPointが重要
                uavs.push_back(uavInfo);
            }
        }

        // === テクスチャの抽出 ===
        textures.clear();
        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc;
            reflection->GetResourceBindingDesc(i, &bindDesc);

            if (bindDesc.Type == D3D_SIT_TEXTURE)
            {
                TextureInfo texInfo;
                texInfo.name      = bindDesc.Name;
                texInfo.nameHash  = CCL::Utils::HashString(bindDesc.Name);
                texInfo.bindPoint = bindDesc.BindPoint;
                texInfo.type      = bindDesc.Type;
                texInfo.dimension = bindDesc.Dimension;

                textures.push_back(texInfo);
            }
        }

        // === サンプラーの抽出 ===
        samplers.clear();
        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc;
            reflection->GetResourceBindingDesc(i, &bindDesc);

            if (bindDesc.Type == D3D_SIT_SAMPLER)
            {
                SamplerInfo sampInfo;
                sampInfo.name      = bindDesc.Name;
                sampInfo.nameHash  = CCL::Utils::HashString(bindDesc.Name);
                sampInfo.bindPoint = bindDesc.BindPoint;

                samplers.push_back(sampInfo);
            }
        }
    }

    const ConstantBufferInfo *ReflectionData::FindConstantBuffer(const std::string &name) const
    {
        for (const auto &cb : constantBuffers)
        {
            if (cb.name == name) return &cb;
        }
        return nullptr;
    }

    const ConstantBufferInfo *ReflectionData::FindConstantBufferBySlot(UINT slot) const
    {
        for (const auto &cb : constantBuffers)
        {
            if (cb.bindPoint == slot) return &cb;
        }
        return nullptr;
    }

    const TextureInfo *ReflectionData::FindTexture(const std::string &name) const
    {
        for (const auto &tex : textures)
        {
            if (tex.name == name) return &tex;
        }
        return nullptr;
    }

    const SamplerInfo *ReflectionData::FindSampler(const std::string &name) const
    {
        for (const auto &samp : samplers)
        {
            if (samp.name == name) return &samp;
        }
        return nullptr;
    }

    void ReflectionData::PrintInfo() const
    {
        CCL_LOG_INFO(LogCategory::Render, "=== Shader Reflection Info ===");

        CCL_LOG_INFO(LogCategory::Render, "Constant Buffers:");
        for (const auto &cb : constantBuffers)
        {
            CCL_LOG_INFO(LogCategory::Render, "  [%s] Slot: b%d, Size: %d bytes",
                         cb.name.c_str(), cb.bindPoint, cb.size);

            for (const auto &var : cb.variables)
            {
                CCL_LOG_INFO(LogCategory::Render, "    - %s (Offset: %d, Size: %d)",
                             var.name.c_str(), var.offset, var.size);
            }
        }

        CCL_LOG_INFO(LogCategory::Render, "Textures:");
        for (const auto &tex : textures)
        {
            CCL_LOG_INFO(LogCategory::Render, "  [%s] Slot: t%d", tex.name.c_str(), tex.bindPoint);
        }

        CCL_LOG_INFO(LogCategory::Render, "Samplers:");
        for (const auto &samp : samplers)
        {
            CCL_LOG_INFO(LogCategory::Render, "  [%s] Slot: s%d", samp.name.c_str(), samp.bindPoint);
        }
    }

    const UnorderedAccessViewInfo *ReflectionData::FindUAV(const std::string &name) const
    {
        for (const auto &uav : uavs)
        {
            if (uav.name == name) return &uav;
        }
        return nullptr;
    }

    // === AutoConstantBuffer の実装 ===

    bool AutoConstantBuffer::Create(ID3D11Device *device, const ConstantBufferInfo &info)
    {
        bufferInfo = info;
        cpuBuffer.resize(info.size, 0); // 1: CPU側のデータ置き場を作る

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth         = info.size; // 2: シェーダーが要求した通りのサイズ
        desc.Usage             = D3D11_USAGE_DEFAULT;
        desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;

        // 3: GPUへの小包を作る
        HRESULT hr = device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
        return SUCCEEDED(hr);
    }

    void AutoConstantBuffer::UpdateBuffer(ID3D11DeviceContext *dc)
    {
        if (isDirty && buffer)
        {
            // 3: CPUの作業台(cpuBuffer)の中身を、GPUの小包(buffer)へ一気に転送する
            dc->UpdateSubresource(buffer.Get(), 0, nullptr, cpuBuffer.data(), 0, 0);
            isDirty = false;
        }
    }

    void AutoConstantBuffer::BindVS(ID3D11DeviceContext *dc) const
    {
        if (buffer)
        {
            dc->VSSetConstantBuffers(bufferInfo.bindPoint, 1, buffer.GetAddressOf());
        }
    }

    void AutoConstantBuffer::BindPS(ID3D11DeviceContext *dc) const
    {
        if (buffer)
        {
            // ステップ1で取得したスロット番号(bindPoint)に、
            // 　小包(buffer)をガチャンと挿し込む
            dc->PSSetConstantBuffers(bufferInfo.bindPoint, 1, buffer.GetAddressOf());
        }
    }

    void AutoConstantBuffer::BindGS(ID3D11DeviceContext *dc) const
    {
        if (buffer)
        {
            dc->GSSetConstantBuffers(bufferInfo.bindPoint, 1, buffer.GetAddressOf());
        }
    }

    void AutoConstantBuffer::BindCS(ID3D11DeviceContext *dc) const
    {
        if (buffer)
        {
            dc->CSSetConstantBuffers(bufferInfo.bindPoint, 1, buffer.GetAddressOf());
        }
    }

    // 追加実装
    void AutoConstantBuffer::SetRawData(const void *data, size_t size)
    {
        if (cpuBuffer.empty()) return;

        // バッファサイズを超えないように安全策
        size_t copySize = (size < cpuBuffer.size()) ? size : cpuBuffer.size();

        // メモリコピー
        std::memcpy(cpuBuffer.data(), data, copySize);
        isDirty = true;
    }
} // namespace ShaderReflect