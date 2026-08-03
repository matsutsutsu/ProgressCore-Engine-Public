#pragma once
#include <d3d11.h>
#include <wrl.h>
#include <vector>
#include <DirectXMath.h>

// GPUに送る1個分のインスタンスデータ
struct InstanceData
{
    DirectX::XMFLOAT4X4 world; // ワールド行列
    // 汎用パラメータ領域（x=ディゾルブ閾値, y=ダメージ点滅フラグ, など）
    DirectX::XMFLOAT4 customParams;
};

class InstanceBuffer
{
public:
    // 外部から参照できる定数
    static const size_t MAX_INSTANCES = 4096;

    // 初期化（最大インスタンス数を指定）
    void Create(ID3D11Device* device, size_t maxInstances = 4096)
    {
        m_maxInstances = maxInstances;

        // StructuredBuffer の作成
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(sizeof(InstanceData) * maxInstances);
        desc.Usage = D3D11_USAGE_DYNAMIC; // CPUから頻繁に書き換える
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(InstanceData);

        device->CreateBuffer(&desc, nullptr, m_buffer.GetAddressOf());

        // シェーダーリソースビュー (SRV) の作成
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(maxInstances);

        device->CreateShaderResourceView(m_buffer.Get(), &srvDesc, m_srv.GetAddressOf());
    }

    // マップ（書き込み開始）
    // 直接ポインタを取得して書き込むことで高速化します
    InstanceData* Map(ID3D11DeviceContext* dc)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            return static_cast<InstanceData*>(mapped.pData);
        }
        return nullptr;
    }

    // アンマップ（書き込み終了）
    void Unmap(ID3D11DeviceContext* dc)
    {
        dc->Unmap(m_buffer.Get(), 0);
    }

    // データの書き込み
    void Write(ID3D11DeviceContext* dc, const std::vector<InstanceData>& data)
    {
        if (data.empty()) return;

        // バッファサイズを超えないように制限
        size_t count = (std::min)(data.size(), m_maxInstances);

        //// 足りない場合は、現在の2倍のサイズで作り直す（動的リサイズ）
        //if (data.size() > m_maxInstances)
        //{
        //    size_t newSize = std::max(data.size(), m_maxInstances * 2);
        //    Create(device, newSize); // 新しいサイズで再確保
        //}

        InstanceData* ptr = Map(dc);
        if (ptr)
        {
            memcpy(ptr, data.data(), sizeof(InstanceData) * count);
            Unmap(dc);
        }
    }

    // データの書き込み（ポインタと要素数を直接受け取る高速版）
    void Write(ID3D11DeviceContext* dc, const InstanceData* data, size_t count)
    {
        if (count == 0 || !data) return;
        size_t writeCount = (std::min)(count, m_maxInstances);
        InstanceData* ptr = Map(dc);
        if (ptr)
        {
            memcpy(ptr, data, sizeof(InstanceData) * writeCount);
            Unmap(dc);
        }
    }

    // 頂点シェーダーにバインド
    void Bind(ID3D11DeviceContext* dc, UINT slot)
    {
        dc->VSSetShaderResources(slot, 1, m_srv.GetAddressOf());
    }

    // 解除
    void Unbind(ID3D11DeviceContext* dc, UINT slot)
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        dc->VSSetShaderResources(slot, 1, &nullSRV);
    }

    // InstanceBuffer.h に追加
    size_t GetMaxInstances() const { return m_maxInstances; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    size_t m_maxInstances = 0;
};