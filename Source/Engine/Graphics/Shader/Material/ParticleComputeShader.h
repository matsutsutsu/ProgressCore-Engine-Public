#pragma once
// ===========================================================================
// 概要: GPU パーティクルの初期化と更新を行うコンピュートシェーダーの組
//
// 責務:
//   - 開発中のビルドでは HLSL をその場でコンパイルし、製品ビルドでは .cso を読む
//   - HLSL が更新されたらコンパイルし直す
//
// 備考: ShaderRegistry には登録しない。GPUParticleUpdateSystem が持ち、
//       Dispatch と CheckAndReload もそちらが呼ぶ
// ===========================================================================
#include "Engine/Graphics/Shader/Core/Shader.h"

class ParticleComputeShader : public Shader
{
public:
    ParticleComputeShader(ID3D11Device *device);
    ~ParticleComputeShader() override = default;

    // Begin は何もしない。End はコンピュートシェーダーを外す
    void Begin(ID3D11DeviceContext *dc, const RenderServices &s, const RenderFrameContext &ctx) override;
    void End(ID3D11DeviceContext *dc, const RenderServices &s) override;

    // HLSL が更新されていたらコンパイルし直す（GPUParticleUpdateSystem が毎フレーム呼ぶ）
    void CheckAndReload(ID3D11Device *device) override;

    ID3D11ComputeShader *GetInitCS() const noexcept { return m_initCS.Get(); }
    ID3D11ComputeShader *GetUpdateCS() const noexcept { return m_updateCS.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_initCS;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_updateCS;

    std::wstring                    m_initCsPath;
    std::wstring                    m_updateCsPath;
    std::filesystem::file_time_type m_initCsTime; // 前に見た HLSL の更新時刻
    std::filesystem::file_time_type m_updateCsTime;

    // 2本が include する共通部分（spawn() など）。ここだけ直しても読み直されるように見る
    std::wstring                    m_includePath;
    std::filesystem::file_time_type m_includeTime;

    // 2本とも読めたら true。開発中は失敗したほうを前のシェーダーのまま残す
    bool LoadShaders(ID3D11Device *device);
};