#pragma once
// ===========================================================================
// 概要: 中間バッファの SRV を、種別に応じて表示できる色へ変換するヘルパー（RenderDebugWindow 用）
//
// 責務:
//   - 深度の線形化・法線の [-1,1]→[0,1] 変換・単チャンネルのグレー表示・HDR の露出・配列スライスの選択
//   - FullScreenVS と RenderTargetDebugPS で内部の RenderTarget へ全画面描画し、その SRV を返す
//   - 描画の前後でパイプラインの状態を退避・復元する
//
// 備考: エディタ専用（Release 構成ではビルドから除外）。RenderDebugWindow が所有する
//       サムネイル用なので変換後の解像度は上限で抑え、RenderTarget はフレームごとに使い回す
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <cstdint>

class RenderTarget;

// 可視化パラメータ。RenderDebugWindow が種別と UI から組み立てる
struct RTVisualizeParams
{
    int   mode          = 0;       // DebugTexKind の整数値
    float exposure      = 1.0f;    // HDR 表示の露出
    float nearZ         = 0.1f;    // 深度の線形化に使う near
    float farZ          = 1000.0f; // 同じく far
    int   arraySlice    = 0; // 配列テクスチャの表示スライス
    int   useArray      = 0; // 0=Texture2D / 1=Texture2DArray
    int   channelSelect = 0; // 0=RGB 1=R 2=G 3=B 4=A
};

class RenderTargetVisualizer
{
public:
    // シェーダーなどを初回だけ作る。失敗したら false
    bool EnsureInitialized(ID3D11Device *device);

    // フレーム先頭で RT プールのインデックスをリセットする
    void BeginFrame() noexcept { m_poolIndex = 0; }

    /**
     * @brief src を変換して内部の RenderTarget へ描き、その SRV を返す
     * @param dc  描画に使うデバイスコンテキスト
     * @param src 表示したいテクスチャ
     * @param w   src の幅（px）。0 なら既定の幅で描く
     * @param h   src の高さ（px）。縦横比を保つのに使う
     * @param p   表示の仕方（種類・露出・チャンネルなど）
     * @return 変換した SRV。失敗したら src をそのまま返す
     *         同じ RenderTarget が次に使い回されるまで有効（そのフレームの ImGui 描画までは使える）
     */
    ID3D11ShaderResourceView *Visualize(ID3D11DeviceContext      *dc,
                                        ID3D11ShaderResourceView *src, uint32_t w, uint32_t h,
                                        const RTVisualizeParams &p);

private:
    // b0 の中身（HLSL CbDebug と一致させること）
    struct Cb
    {
        int   mode;
        float exposure;
        float nearZ;
        float farZ;
        int   arraySlice;
        int   useArray;
        int   channelSelect;
        int   _pad0;
    };

    /**
     * @brief このフレームで次に使う変換先の RenderTarget を取り出す。なければ作り、大きさが違えば作り直す
     * @param device 作るときに使うデバイス
     * @param w      必要な幅（px）
     * @param h      必要な高さ（px）
     * @return 変換先。プールが所有する。作れなければ nullptr
     */
    RenderTarget *AcquirePoolTarget(ID3D11Device *device, uint32_t w, uint32_t h);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_cb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    // 直前のパス（SSAO・Bloom 等）が加算・乗算のブレンドや深度テストを残したまま
    //   全画面三角形を描くと、意図した色で上書きされず黒く見える
    //   他のポストエフェクト（SSAOEffect 等）と同じく、不透明・深度なしを明示的に設定する
    Microsoft::WRL::ComPtr<ID3D11BlendState>        m_opaqueBlendState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_noDepthState;

    std::vector<std::unique_ptr<RenderTarget>> m_pool;            // 表示するエントリの数だけ作り、毎フレーム使い回す
    size_t                                     m_poolIndex   = 0; // このフレームで次に使う m_pool の添字
    bool                                       m_initialized = false;
};
