#pragma once
// ===========================================================================
// 概要: 各 RenderPass がデバッグ表示したい途中のテクスチャ（SRV）を、毎フレーム登録する一覧。
//
// 責務:
//   - {名前, SRV, サイズ, 種類} の一覧を1フレーム分持つ
//   - RenderDebugWindow に一覧を渡す
//
// 備考: SSAO やポストプロセスの途中のレンダーターゲットは各クラスの private にある。
//       UI 側から見るためにクラスごとに getter を足していくと増え続けるので、
//       SRV を持っているパスが自分で登録する。
//       エディタ入りのビルド（ENABLE_EDITOR）でだけ中身を持ち、それ以外では Register は何もしない。
//       BaseScene がフレームの最初に BeginFrame を呼ぶ（RenderQueue::BeginFrame の隣）。
// ===========================================================================
#include <d3d11.h>
#include <vector>
#include <cstdint>

// テクスチャの中身の種類。RenderTargetVisualizer が表示するときの変換を選ぶのに使う
enum class DebugTexKind : uint8_t
{
    Color,         // LDR / SDR の色。そのまま表示する
    HDRColor,      // HDR の色。露出をかけて表示する
    Depth,         // 深度。線形にして表示する
    SingleChannel, // R だけ（AO など）。グレーで表示する
    Normal,        // 法線。[-1, 1] を [0, 1] に直して表示する
    Array,         // Texture2DArray（カスケードシャドウなど）。スライスを選んで表示する
};

// デバッグ表示1件分。SRV は、そのフレームで描画先から外した後のものを渡す
struct DebugTexEntry
{
    const char               *name   = nullptr; // フレームをまたいで有効な文字列（文字列リテラルなど）
    ID3D11ShaderResourceView *srv    = nullptr; // 所有しない
    uint32_t                  width  = 0;       // px
    uint32_t                  height = 0;       // px
    DebugTexKind              kind   = DebugTexKind::Color;
};

class RenderDebugRegistry
{
public:
    static RenderDebugRegistry &Instance()
    {
        static RenderDebugRegistry s;
        return s;
    }

    // 一覧を空にする（確保済みの容量は残す）
    void BeginFrame() noexcept { m_entries.clear(); }

    // 途中のテクスチャを1件登録する
    void Register(const DebugTexEntry &e)
    {
#ifdef ENABLE_EDITOR
        if (!e.srv) return;
        m_entries.push_back(e);
#else
        (void)e;
#endif
    }

    const std::vector<DebugTexEntry> &GetEntries() const noexcept { return m_entries; }

private:
    RenderDebugRegistry() { m_entries.reserve(16); }

    std::vector<DebugTexEntry> m_entries;
};
