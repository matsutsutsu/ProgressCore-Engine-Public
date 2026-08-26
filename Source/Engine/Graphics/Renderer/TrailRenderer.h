#pragma once
// ===========================================================================
// TrailRenderer.h
// ---------------
// 役割:
//   TrailSystem が積んだ三角形頂点を、1フレーム1ドローで加算描画する常駐レンダラ。
//
// 設計方針:
//   - RenderServices 構造体を変更せず配線を最小化するため、RenderQueue と同じく
//     シングルトンにする。GPU リソースは初回 Render 時に device から遅延生成する。
//   - ブレンドは Additive、深度は TestOnly(書き込み無し)、両面描画(CullNone)。
//     RTV/DSV は先行パス(SetupPass 等)がバインド済みの HDR ターゲットをそのまま使う。
//   - 頂点はワールド空間で積み、VS で viewProjection(row_major) だけ掛ける。
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <mutex>

#include "Engine/Graphics/RHI/PipelineState.h"

struct RenderServices;
struct RenderFrameContext;

class TrailRenderer
{
public:
    /// @brief 加算リボンの1頂点（ワールド座標＋頂点カラー）。
    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color; ///< rgb=加算色 / a=強度(尾ほど小さくフェード)
    };

    static TrailRenderer& Instance()
    {
        static TrailRenderer instance;
        return instance;
    }

    /// @brief ECS(TrailSystem)から三角形リストの頂点を積む（スレッド安全）。
    /// @note  vertexCount は3の倍数を想定。容量超過分は三角形境界で切り捨てる。
    void SubmitTriangleList(const Vertex* verts, size_t vertexCount);

    /// @brief TrailPass から毎フレーム呼ばれる。溜めた頂点を描画してリストを空にする。
    void Render(const RenderServices& services, const RenderFrameContext& ctx);

    /// @brief GPU リソースを解放する（エンジン終了時）。
    void Shutdown();

private:
    TrailRenderer() = default;
    ~TrailRenderer() = default;
    TrailRenderer(const TrailRenderer&) = delete;
    TrailRenderer& operator=(const TrailRenderer&) = delete;

    /// @brief 初回のみ device からシェーダ/入力レイアウト/動的バッファ/PSO を生成。
    void EnsureInitialized(ID3D11Device* device);

    /// @brief 1フレーム上限。筒(Tube)は1本で約3,300頂点使うため、板だけの頃の
    ///        8192 では足元2本＋剣で溢れる。32768頂点でも動的VBは約0.9MB。
    static constexpr size_t MAX_VERTICES = 32768;

    bool m_initialized = false;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_vertexBuffer;   ///< DYNAMIC 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer; ///< b0: viewProjection

    PipelineState m_pso; ///< Additive / TestOnly / CullNone / TriangleList

    std::vector<Vertex> m_vertices; ///< フレーム内の全トレイル頂点
    std::mutex          m_mutex;
};
