#include "ShaderReflection.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"

namespace ShaderReflect
{
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



    bool ReflectionData::LoadFromCompiledFile(const char *filename)
    {
        const std::vector<uint8_t> bytecode = FileSystem::ReadAllBytes(filename);
        if (bytecode.empty())
        {
            CCL_LOG_ERROR(LogCategory::Render, "Failed to open compiled shader file: %s", filename);
            return false;
        }

        return LoadFromBytecode(bytecode.data(), bytecode.size());
    }



    bool ReflectionData::LoadFromBytecode(const void *bytecode, size_t bytecodeSize)
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;

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

        ExtractReflectionInfo(reflection.Get());
        return true;
    }



    void ReflectionData::MergeFromBytecode(const void *bytecode, size_t bytecodeSize)
    {
        // LoadFromBytecode は前の結果を消すので、別のオブジェクトに読んでから名前で重複を除いて足す
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



    void ReflectionData::ExtractReflectionInfo(ID3D11ShaderReflection *reflection)
    {
        D3D11_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

        // 定数バッファ
        constantBuffers.clear();
        for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
        {
            ID3D11ShaderReflectionConstantBuffer *cb = reflection->GetConstantBufferByIndex(i);
            D3D11_SHADER_BUFFER_DESC              bufferDesc;
            cb->GetDesc(&bufferDesc);

            ConstantBufferInfo cbInfo;
            cbInfo.name = bufferDesc.Name;
            cbInfo.size = bufferDesc.Size;

            // 定数バッファの説明にはスロットがないので、同じ名前のリソースのバインド情報から取る
            for (UINT j = 0; j < shaderDesc.BoundResources; ++j)
            {
                D3D11_SHADER_INPUT_BIND_DESC bindDesc;
                reflection->GetResourceBindingDesc(j, &bindDesc);

                if (bindDesc.Type == D3D_SIT_CBUFFER &&
                    strcmp(bindDesc.Name, bufferDesc.Name) == 0)
                {
                    cbInfo.bindPoint = bindDesc.BindPoint;
                    break;
                }
            }

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

        // UAV
        uavs.clear();
        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc;
            reflection->GetResourceBindingDesc(i, &bindDesc);

            if (bindDesc.Type == D3D_SIT_UAV_RWTYPED || bindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_RWBYTEADDRESS)
            {
                UnorderedAccessViewInfo uavInfo;
                uavInfo.name      = bindDesc.Name;
                uavInfo.bindPoint = bindDesc.BindPoint;
                uavs.push_back(uavInfo);
            }
        }

        // テクスチャ
        textures.clear();
        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc;
            reflection->GetResourceBindingDesc(i, &bindDesc);

            if (bindDesc.Type == D3D_SIT_TEXTURE)
            {
                TextureInfo texInfo;
                texInfo.name        = bindDesc.Name;
                texInfo.nameHash    = CCL::Utils::HashString(bindDesc.Name);
                texInfo.useFlagHash = CCL::Utils::HashString(std::string("use") + bindDesc.Name);
                texInfo.bindPoint   = bindDesc.BindPoint;
                texInfo.type        = bindDesc.Type;
                texInfo.dimension   = bindDesc.Dimension;

                textures.push_back(texInfo);
            }
        }

        // サンプラー
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



    // ============================================================
    // AutoConstantBuffer
    // ============================================================

    bool AutoConstantBuffer::Create(ID3D11Device *device, const ConstantBufferInfo &info)
    {
        bufferInfo = info;
        cpuBuffer.resize(info.size, 0);

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth         = info.size; // リフレクションの大きさは 16 バイトの倍数になっている
        desc.Usage             = D3D11_USAGE_DEFAULT;
        desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;

        HRESULT hr = device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
        return SUCCEEDED(hr);
    }



    void AutoConstantBuffer::UpdateBuffer(ID3D11DeviceContext *dc)
    {
        if (isDirty && buffer)
        {
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



    void AutoConstantBuffer::SetRawData(const void *data, size_t size)
    {
        if (cpuBuffer.empty()) return;

        size_t copySize = (size < cpuBuffer.size()) ? size : cpuBuffer.size();
        std::memcpy(cpuBuffer.data(), data, copySize);
        isDirty = true;
    }
}