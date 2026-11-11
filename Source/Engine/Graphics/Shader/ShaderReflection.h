#pragma once
// ===========================================================================
// 概要: コンパイル済みのシェーダーから、定数バッファ・テクスチャ・サンプラー・UAV の情報を読むクラスと、
//       その情報から作る定数バッファ（AutoConstantBuffer）
//
// 責務:
//   - D3DReflect で .cso やバイトコードを調べ、名前・スロット・大きさ・変数の位置を集める
//   - 定数バッファの変数に名前（またはそのハッシュ）で値を書き、まとめて GPU に送る
//
// 備考: Shader がこれを使い、HLSL の cbuffer の中身を C++ 側で定義しなくても
//       マテリアルの値を名前で送れるようにしている
// ===========================================================================
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <DirectXMath.h>

#include "Engine/Core/Math/StringHash.h"

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace ShaderReflect
{
    // 定数バッファの中の変数1つ
    struct VariableInfo
    {
        std::string              name;
        uint32_t                 nameHash = 0;            // 名前のハッシュ（CCL::Utils::HashString）
        UINT                     offset   = 0;            // バッファの先頭からの位置（バイト）
        UINT                     size     = 0;            // 大きさ（バイト）
        D3D_SHADER_VARIABLE_TYPE type     = D3D_SVT_VOID; // float・int など
        UINT                     rows     = 0;            // 行の数（ベクトル・行列）
        UINT                     columns  = 0;            // 列の数（ベクトル・行列）
    };

    // 定数バッファ1つと、その中の変数の一覧
    struct ConstantBufferInfo
    {
        std::string               name;
        UINT                      bindPoint = 0; // レジスタの番号（b0 なら 0）
        UINT                      size      = 0; // 大きさ（バイト）
        std::vector<VariableInfo> variables;

        const VariableInfo *FindVariable(const std::string &varName) const;
        // 名前のハッシュで探す（文字列を比べないので速い）
        const VariableInfo *FindVariable(uint32_t nameHash) const;
    };

    // UAV（RWStructuredBuffer など）
    struct UnorderedAccessViewInfo
    {
        std::string name;
        UINT        bindPoint = 0; // レジスタの番号（u0 なら 0）
        UINT        size      = 0; // 今は読んでいない（常に 0）
    };

    // テクスチャ
    struct TextureInfo
    {
        std::string           name;
        uint32_t              nameHash    = 0;
        uint32_t              useFlagHash = 0;                         // "use" + name のハッシュ（AlbedoMap なら useAlbedoMap）
        UINT                  bindPoint   = 0;                         // レジスタの番号（t0 なら 0）
        D3D_SHADER_INPUT_TYPE type        = D3D_SIT_CBUFFER;           // 読んだ後は D3D_SIT_TEXTURE
        D3D_SRV_DIMENSION     dimension   = D3D_SRV_DIMENSION_UNKNOWN; // Texture2D・Texture2DArray など
    };

    // サンプラー
    struct SamplerInfo
    {
        std::string name;
        uint32_t    nameHash  = 0;
        UINT        bindPoint = 0; // レジスタの番号（s0 なら 0）
    };

    // 1つのシェーダー（または合成した複数のシェーダー）のリフレクションの結果
    class ReflectionData
    {
    public:
        ReflectionData() = default;

        // .cso を読んで調べる（前の結果は消える）
        bool LoadFromCompiledFile(const char *filename);

        // バイトコードを調べる（前の結果は消える）
        bool LoadFromBytecode(const void *bytecode, size_t bytecodeSize);

        // 今の結果に、別のステージのバイトコードの結果を足す（頂点とピクセルのシェーダーを1つにまとめるとき）
        // 同じ名前のものは同じリソースとみなし、先に入っている方を残す
        void MergeFromBytecode(const void *bytecode, size_t bytecodeSize);

        const std::vector<ConstantBufferInfo> &GetConstantBuffers() const { return constantBuffers; }
        const std::vector<TextureInfo>        &GetTextures() const { return textures; }
        const std::vector<SamplerInfo>        &GetSamplers() const { return samplers; }

        const ConstantBufferInfo *FindConstantBuffer(const std::string &name) const;
        const ConstantBufferInfo *FindConstantBufferBySlot(UINT slot) const;
        const TextureInfo        *FindTexture(const std::string &name) const;
        const SamplerInfo        *FindSampler(const std::string &name) const;

        // テクスチャの名前からスロットの番号を引く。なければ -1
        int FindTextureSlot(const std::string &name) const
        {
            for (const auto &tex : textures)
            {
                if (tex.name == name) return (int)tex.bindPoint;
            }
            return -1;
        }

        // サンプラーの名前からスロットの番号を引く。なければ -1
        int FindSamplerSlot(const std::string &name) const
        {
            for (const auto &samp : samplers)
            {
                if (samp.name == name) return (int)samp.bindPoint;
            }
            return -1;
        }

        // 調べた結果をログに出す（デバッグ用）
        void PrintInfo() const;

        const std::vector<UnorderedAccessViewInfo> &GetUAVs() const { return uavs; }
        const UnorderedAccessViewInfo              *FindUAV(const std::string &name) const;

    private:
        void ExtractReflectionInfo(ID3D11ShaderReflection *reflection); // 定数バッファ・テクスチャ・サンプラー・UAV の情報を読み出す

        std::vector<ConstantBufferInfo> constantBuffers;
        std::vector<TextureInfo>        textures;
        std::vector<SamplerInfo>        samplers;

        std::vector<UnorderedAccessViewInfo> uavs;
    };

    // リフレクションで分かった形の定数バッファ。CPU 側に写しを持ち、変数を名前で書いてから GPU に送る
    class AutoConstantBuffer
    {
    public:
        AutoConstantBuffer() = default;

        // info の大きさで、CPU 側の写しと GPU のバッファ（DEFAULT）を作る
        bool Create(ID3D11Device *device, const ConstantBufferInfo &info);

        // varName の変数の位置に value を書く（送るのは UpdateBuffer）
        // 変数がない・value が変数より大きいなら false
        template <typename T>
        bool SetValue(const std::string &varName, const T &value)
        {
            const VariableInfo *varInfo = bufferInfo.FindVariable(varName);
            if (!varInfo) return false;
            if (sizeof(T) > varInfo->size) return false;

            memcpy(cpuBuffer.data() + varInfo->offset, &value, sizeof(T));
            isDirty = true;
            return true;
        }

        // 名前のハッシュで変数を探して書く
        template <typename T>
        bool SetValue(uint32_t nameHash, const T &value)
        {
            const VariableInfo *varInfo = bufferInfo.FindVariable(nameHash);
            if (!varInfo) return false;
            if (sizeof(T) > varInfo->size) return false;

            memcpy(cpuBuffer.data() + varInfo->offset, &value, sizeof(T));
            isDirty = true;
            return true;
        }

        // 書き換えていれば、CPU 側の写しを GPU に送る
        void UpdateBuffer(ID3D11DeviceContext *dc);

        // bindPoint のスロットにバインドする
        void BindVS(ID3D11DeviceContext *dc) const;
        void BindPS(ID3D11DeviceContext *dc) const;
        void BindGS(ID3D11DeviceContext *dc) const;
        void BindCS(ID3D11DeviceContext *dc) const;

        ID3D11Buffer             *GetBuffer() const { return buffer.Get(); }
        UINT                      GetBindPoint() const { return bufferInfo.bindPoint; }
        const ConstantBufferInfo &GetInfo() const { return bufferInfo; }

        // data を先頭から書く（バッファより大きいぶんは切り捨てる）
        void SetRawData(const void *data, size_t size);

        // CPU 側の写しを全部 0 にする。前のマテリアルの値が残らないよう、Shader::UpdateMaterialProperties が呼ぶ
        void ResetToZero()
        {
            std::fill(cpuBuffer.begin(), cpuBuffer.end(), static_cast<BYTE>(0));
            isDirty = true;
        }

    private:
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        ConstantBufferInfo                   bufferInfo;
        std::vector<BYTE>                    cpuBuffer;       // GPU に送る前の CPU 側の写し
        bool                                 isDirty = false; // 前に送ってから書き換えたか
    };
} // namespace ShaderReflect