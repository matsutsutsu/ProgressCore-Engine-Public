#pragma once
// ===========================================================================
// 概要: インスタンシング描画で使う、インスタンスごとのデータの StructuredBuffer。
//
// 責務:
//   - DYNAMIC の StructuredBuffer と、その SRV を作る
//   - CPU から書き込み（Map / Unmap）、頂点シェーダーにバインドする
//
// 備考: 容量（最大インスタンス数）は Create で決まる。それより多く書こうとしたぶんは切り捨てる。
// ===========================================================================
#include <d3d11.h>
#include <wrl.h>
#include <vector>
#include <DirectXMath.h>
#include "Engine/Platform/Core/Logger.h"

// GPU に送るインスタンス1個分のデータ
struct InstanceData
{
    DirectX::XMFLOAT4X4 world; // ワールド行列

    // シェーダーごとに意味を決める4つの値。
    // Phong では z = 被弾フラッシュの強さ。
    // ShapeRenderer / PrimitiveRenderer では図形の色
    DirectX::XMFLOAT4 customParams;
};

class InstanceBuffer
{
public:
    /**
     * @brief maxInstances 個分のバッファを作る。
     *
     * 容量を知りたいときは必ず GetMaxInstances() を使う。容量を表す定数を別に作ると、
     * この既定の引数と食い違ったとき、Write が切り捨てた先のまだ書いていないインスタンスを
     * DrawInstanced が読んで描いてしまう。エラーは出ないので、見た目でしか気付けない。
     */
    void Create(ID3D11Device *device, size_t maxInstances = 4096)
    {
        m_maxInstances = maxInstances;

        D3D11_BUFFER_DESC desc   = {};
        desc.ByteWidth           = static_cast<UINT>(sizeof(InstanceData) * maxInstances);
        desc.Usage               = D3D11_USAGE_DYNAMIC; // 毎回 CPU から書き換える
        desc.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(InstanceData);

        if (FAILED(device->CreateBuffer(&desc, nullptr, m_buffer.GetAddressOf())))
        {
            // 作れないと Map が毎回 null を返し、エラーも出ないまま何も描かれなくなるのでログに出す
            CCL_LOG_ERROR(LogCategory::Render,
                          "[InstanceBuffer] インスタンスバッファの生成に失敗しました (max=%zu)", maxInstances);
            m_maxInstances = 0;
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement             = 0;
        srvDesc.Buffer.NumElements              = static_cast<UINT>(maxInstances);

        if (FAILED(device->CreateShaderResourceView(m_buffer.Get(), &srvDesc, m_srv.GetAddressOf())))
        {
            CCL_LOG_ERROR(LogCategory::Render, "[InstanceBuffer] インスタンス SRV の生成に失敗しました。");
            m_maxInstances = 0;
        }
    }

    // 書き込み先のポインタを返す（WRITE_DISCARD。前の中身は消える）。失敗したら nullptr
    InstanceData *Map(ID3D11DeviceContext *dc)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            return static_cast<InstanceData *>(mapped.pData);
        }
        return nullptr;
    }

    void Unmap(ID3D11DeviceContext *dc)
    {
        dc->Unmap(m_buffer.Get(), 0);
    }

    // data を先頭から書き込む。容量を超えたぶんは切り捨てる（描かれない）
    void Write(ID3D11DeviceContext *dc, const std::vector<InstanceData> &data)
    {
        if (data.empty()) return;

        size_t count = (std::min)(data.size(), m_maxInstances);

        InstanceData *ptr = Map(dc);
        if (ptr)
        {
            memcpy(ptr, data.data(), sizeof(InstanceData) * count);
            Unmap(dc);
        }
    }

    // data から count 個を書き込む（配列の一部を vector に写さずに渡せる）
    void Write(ID3D11DeviceContext *dc, const InstanceData *data, size_t count)
    {
        if (count == 0 || !data) return;
        size_t        writeCount = (std::min)(count, m_maxInstances);
        InstanceData *ptr        = Map(dc);
        if (ptr)
        {
            memcpy(ptr, data, sizeof(InstanceData) * writeCount);
            Unmap(dc);
        }
    }

    // 頂点シェーダーの slot にバインドする
    void Bind(ID3D11DeviceContext *dc, UINT slot)
    {
        dc->VSSetShaderResources(slot, 1, m_srv.GetAddressOf());
    }

    void Unbind(ID3D11DeviceContext *dc, UINT slot)
    {
        ID3D11ShaderResourceView *nullSRV = nullptr;
        dc->VSSetShaderResources(slot, 1, &nullSRV);
    }

    // 1回に書き込めるインスタンスの数（作れなかったときは 0）
    size_t GetMaxInstances() const { return m_maxInstances; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    size_t                                           m_maxInstances = 0;
};
