#pragma once
// ===========================================================================
// 概要: TrailSystem が積んだトレイルの三角形を、少ないドローコールで描くクラス
//
// 責務:
//   - 加算の層と歪みの層の2つの頂点の列をためる
//   - TrailPass から呼ばれ、歪み、加算の順に描いて列を空にする
//
// 備考: 歪みの層は SceneColorGrabPass が写した色をずらして読む
//       歪みのブレンドは RGB だけ書く。HDR バッファのアルファを壊すと、エディタの GameView が黒く抜ける
//       どちらの層も深度のテストだけして書かず、両面を描く
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
    // トレイルの1頂点。color の意味は層によって違う
    //   加算の層: rgb = 足す色 / a = 強さ（尾ほど小さい）
    //   歪みの層: r = ずらす量（px） / g, b = 使わない / a = 尾のフェード
    struct Vertex
    {
        DirectX::XMFLOAT3 position; // ワールド座標（m）
        DirectX::XMFLOAT4 color;    // 色、または歪みのパラメータ
        DirectX::XMFLOAT2 uv;       // テクスチャ座標（x は尾の長さ方向）
    };

    TrailRenderer()  = default;
    ~TrailRenderer() = default;

    TrailRenderer(const TrailRenderer &)            = delete;
    TrailRenderer &operator=(const TrailRenderer &) = delete;

    // シェーダー・入力レイアウト・バッファ・PSO を作る（Graphics::Initialize から1回だけ呼ぶ）
    void Initialize(ID3D11Device *device);

    // 加算の層に三角形の頂点を積む（別スレッドから呼んでいい）
    // 容量を超えた分は三角形の区切りで捨てる。texture が null なら白で描く
    void SubmitTriangleList(const Vertex *verts, size_t vertexCount,
                            ID3D11ShaderResourceView *texture = nullptr);

    // 歪みの層に三角形の頂点を積む（別スレッドから呼んでいい）
    void SubmitDistortTriangleList(const Vertex *verts, size_t vertexCount);

    // ためた頂点を描いて空にする（TrailPass から毎フレーム呼ばれる）
    void Render(const RenderServices &services, const RenderFrameContext &ctx);

private:
    // あと何頂点積めるか（三角形の区切りに丸める）。m_mutex を持った状態で呼ぶ
    size_t ClampToCapacity(size_t vertexCount) const;

    // ためた頂点を捨てる。m_mutex を持った状態で呼ぶ
    void ClearPending();

    // 1フレームに積める頂点の数（2つの層の合計）
    // 補間ありの筒がリングの満杯まで伸びると、1本で7割ほど使う
    static constexpr size_t MAX_VERTICES = 49152;

    bool m_initialized = false; // Initialize が済んだか

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;       // 両方の層で使う頂点シェーダー
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader;        // 加算の層
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_distortPixelShader; // 歪みの層
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout;        // Vertex の並び
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_vertexBuffer;       // DYNAMIC の頂点バッファ
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_constantBuffer;     // b0: viewProjection

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whiteTex; // テクスチャのないトレイル用の 1x1 の白

    PipelineState m_pso;        // 加算 / 深度はテストだけ / 両面 / TriangleList
    PipelineState m_distortPso; // アルファブレンド（RGB だけ） / 深度はテストだけ / 両面

    // 加算の層のうち、同じテクスチャが続く区間（1回のドローコール）
    // 加算は描く順で結果が変わらないので、テクスチャごとに分けて描いていい
    struct AddBatch
    {
        ID3D11ShaderResourceView *srv   = nullptr; // null は白
        size_t                    count = 0;       // この区間の頂点の数
    };

    std::vector<Vertex>   m_vertices;        // 加算の層の頂点（全区間をつなげたもの）
    std::vector<AddBatch> m_addBatches;      // m_vertices をテクスチャごとに分けた区間
    std::vector<Vertex>   m_distortVertices; // 歪みの層の頂点
    std::mutex            m_mutex;           // 積む側と描く側の排他
};
