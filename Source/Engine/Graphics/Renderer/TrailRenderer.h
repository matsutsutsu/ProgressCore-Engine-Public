#pragma once
// ===========================================================================
// 概要: TrailSystem が積んだトレイルの三角形を、1フレームにつき少ないドローコールで描くクラス。
//
// 責務:
//   - 加算の層と、背景を歪ませる層の2つの頂点の列をためる（別スレッドから積んでもよい）
//   - TrailPass から呼ばれ、歪み → 加算 の順に描いて列を空にする
//
// 備考:
//   - ParticleRenderer と同じく Graphics が所有し、パスへは RenderServices、ECS へは
//     World リソース（TrailRenderer*）で渡す。GPU リソースの寿命がデバイスと揃うので、
//     デバイスロストからの復帰でも作り直される。
//   - 加算の層: ブレンドは Additive、深度はテストだけ（書かない）、両面を描く。
//   - 歪みの層: 同じ形で、SceneColorGrabPass が写したシーンの色をずらして読み、アルファブレンドで重ねる。
//     加算の明るい芯が上に来るよう、歪みを先に描く。
//     ブレンドは RGB だけを書く UITransparency を使う。HDR バッファのアルファ（1）を
//     壊すと、エディタの GameView が黒く抜けるため。
//   - 2つの層は同じ DYNAMIC 頂点バッファに前後に詰め、Draw の開始位置だけ変える。
//   - 描画先は、前のパスがバインドしたままの HDR バッファをそのまま使う。
//   - 頂点はワールド座標で積み、頂点シェーダーで viewProjection（row_major）だけを掛ける。
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
    // トレイルの1頂点。color の意味は層によって違う。
    //   加算の層: rgb = 足す色 / a = 強さ（尾ほど小さい）
    //   歪みの層: r = ずらす量（px） / g = 色のずれ / b = 陽炎の揺らぎ / a = 尾のフェード
    struct Vertex
    {
        DirectX::XMFLOAT3 position; // ワールド座標（m）
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 uv; // x = 尾の長さの方向, y = 幅の方向
    };

    TrailRenderer()  = default;
    ~TrailRenderer() = default;

    TrailRenderer(const TrailRenderer &)            = delete;
    TrailRenderer &operator=(const TrailRenderer &) = delete;

    // シェーダー・入力レイアウト・バッファ・PSO を作る（Graphics::Initialize から1回だけ呼ぶ）
    void Initialize(ID3D11Device *device);

    /**
     * @brief 加算の層に三角形の頂点を積む（別スレッドから呼んでよい）。
     * @param verts       積む頂点の先頭。nullptr なら何もしない。
     * @param vertexCount 3 の倍数。容量を超えたぶんは三角形の区切りで切り捨てる
     * @param texture     このトレイルのテクスチャ。null なら白（頂点の色だけになる）。
     *                    全トレイルを1つのバッファに詰め、テクスチャが変わる所でドローコールを分ける
     */
    void SubmitTriangleList(const Vertex *verts, size_t vertexCount,
                            ID3D11ShaderResourceView *texture = nullptr);

    // 歪みの層に三角形の頂点を積む（別スレッドから呼んでよい）
    void SubmitDistortTriangleList(const Vertex *verts, size_t vertexCount);

    // ためた頂点を描いて空にする（TrailPass から毎フレーム呼ばれる）
    void Render(const RenderServices &services, const RenderFrameContext &ctx);

private:
    // あと何頂点積めるか（三角形の区切りに丸める）。m_mutex を持った状態で呼ぶ
    size_t ClampToCapacity(size_t vertexCount) const;

    // ためた頂点を捨てる。m_mutex を持った状態で呼ぶ
    void ClearPending();

    // 1フレームに積める頂点の数（2つの層の合計）。49152 頂点で頂点バッファは約 1.7MB。
    // 頂点数は 6 ×（断面列の数 − 1）× 面の数。断面列は補間ありで（区間数 × 4）、なしで区間数。
    // 面の数は Ribbon=1 / Cross=2 / Tube=分割数。歪みを付けるとその2倍を積む。
    // リング（MAX_SEGMENTS=256）が満杯のときの目安: 補間なしの4分割の筒で約 6,100、
    // 補間ありの板で約 6,100（歪み付きで約 12,200）。補間ありの6分割の筒は約 36,700 なので、
    // それが満杯まで伸びると、後から積んだトレイルは入りきらずに切り捨てられる。
    static constexpr size_t MAX_VERTICES = 49152;

    bool m_initialized = false; // Initialize が済んだか

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_distortPixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_vertexBuffer;   // DYNAMIC の頂点バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer; // b0: viewProjection

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whiteTex; // テクスチャの無いトレイル用の 1x1 の白

    PipelineState m_pso;        // 加算 / 深度はテストだけ / 両面 / TriangleList
    PipelineState m_distortPso; // アルファブレンド（RGB だけ） / 深度はテストだけ / 両面

    // 加算の層のうち、同じテクスチャが続く区間（1回のドローコール）。
    // 加算は描く順で結果が変わらないので、テクスチャごとに分けて描いてよい
    struct AddBatch
    {
        ID3D11ShaderResourceView *srv   = nullptr; // null は白
        size_t                    count = 0;       // この区間の頂点の数
    };

    std::vector<Vertex>   m_vertices;        // 加算の層の頂点（全区間をつなげたもの）
    std::vector<AddBatch> m_addBatches;      // m_vertices をテクスチャごとに分けた区間
    std::vector<Vertex>   m_distortVertices; // 歪みの層の頂点
    std::mutex            m_mutex;
};
