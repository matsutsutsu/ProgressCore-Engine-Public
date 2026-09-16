#pragma once
// ===========================================================================
// TrailRenderer.h
// ---------------
// 役割:
//   TrailSystem が積んだ三角形頂点を、1フレーム最小ドローで描画する常駐レンダラ。
//   「加算レイヤ」と「歪み(屈折)レイヤ」の2枚を持つ。
//
// 設計方針:
//   - RenderServices 構造体を変更せず配線を最小化するため、RenderQueue と同じく
//     シングルトンにする。GPU リソースは初回 Render 時に device から遅延生成する。
//   - 加算レイヤ: ブレンド Additive、深度 TestOnly(書き込み無し)、両面(CullNone)。
//   - 歪みレイヤ: 同じ形状を αブレンドで背景の屈折像に置き換える。加算の芯が
//     屈折の膜の上に乗るよう、歪み → 加算 の順に描く。
//     RGB のみ書き込むブレンド(UITransparency)を使うのは、HDR ターゲットの
//     α=1 を壊さないため（壊すとエディタ GameView が黒く抜ける）。
//   - 2レイヤーは同じ動的頂点バッファに前後で詰め、Draw の開始位置を変えるだけ。
//   - RTV/DSV は先行パス(SetupPass 等)がバインド済みの HDR ターゲットをそのまま使う。
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
    // トレイルの1頂点（ワールド座標＋頂点カラー）。
    //
    // color の意味はレイヤで異なる。
    // 加算レイヤ: rgb=加算色 / a=強度(尾ほど小さくフェード)
    // 歪みレイヤ: r=歪み量(px) / g=色ズレ / b=陽炎ゆらぎ / a=尾のフェード
    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 uv; // x=尾の長さ方向, y=幅方向。テクスチャ貼付け用。
    };

    static TrailRenderer &Instance()
    {
        static TrailRenderer instance;
        return instance;
    }

    /**
     * @brief ECS(TrailSystem)から加算レイヤの三角形頂点を積む（スレッド安全）。
     * @note  vertexCount は3の倍数を想定。容量超過分は三角形境界で切り捨てる。
     * @param texture このトレイルに貼るテクスチャSRV。null なら白（＝従来の単色）。
     *               全トレイルを1バッファに詰めつつ、SRV 単位でドローを分けて描く。
     */
    void SubmitTriangleList(const Vertex *verts, size_t vertexCount,
                            ID3D11ShaderResourceView *texture = nullptr);

    // 歪みレイヤの三角形頂点を積む（スレッド安全）。形状は加算と同一。
    void SubmitDistortTriangleList(const Vertex *verts, size_t vertexCount);

    // TrailPass から毎フレーム呼ばれる。溜めた頂点を描画してリストを空にする。
    void Render(const RenderServices &services, const RenderFrameContext &ctx);

    // GPU リソースを解放する（エンジン終了時）。
    void Shutdown();

private:
    TrailRenderer()                                 = default;
    ~TrailRenderer()                                = default;
    TrailRenderer(const TrailRenderer &)            = delete;
    TrailRenderer &operator=(const TrailRenderer &) = delete;

    // 初回のみ device からシェーダ/入力レイアウト/動的バッファ/PSO を生成。
    void EnsureInitialized(ID3D11Device *device);

    // m_mutex 保持前提。上限に収まる追加数を返す（三角形境界で丸める）。
    size_t ClampToCapacity(size_t vertexCount) const;

    // 1フレーム上限（2レイヤー合算）。筒(Tube)は1本で約3,300頂点使い、
    // 歪みを付けるとその2倍積むため、板だけの頃の 8192 では全く足りない。
    // 49152頂点でも動的VBは約1.3MB。
    static constexpr size_t MAX_VERTICES = 49152;

    bool m_initialized = false;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_distortPixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_vertexBuffer;   // DYNAMIC 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer; // b0: viewProjection

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whiteTex; // テクスチャ無しトレイル用の1x1白

    PipelineState m_pso;        // 加算 / TestOnly / CullNone / TriangleList
    PipelineState m_distortPso; // αブレンド(RGBのみ) / TestOnly / CullNone

    // 加算レイヤの「テクスチャ単位ドロー範囲」。頂点は m_vertices に連続で詰め、
    // SRV が変わる境界で1バッチにする（加算は順序非依存なので分割自由）。
    struct AddBatch
    {
        ID3D11ShaderResourceView *srv   = nullptr; // null は白テクスチャ
        size_t                    count = 0;       // この範囲の頂点数
    };

    std::vector<Vertex>   m_vertices;        // フレーム内の加算レイヤ頂点（全バッチ連結）
    std::vector<AddBatch> m_addBatches;      // m_vertices を SRV 単位に分ける範囲
    std::vector<Vertex>   m_distortVertices; // フレーム内の歪みレイヤ頂点
    std::mutex            m_mutex;
};
