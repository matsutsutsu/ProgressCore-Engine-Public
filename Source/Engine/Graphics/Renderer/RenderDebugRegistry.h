/// @file  RenderDebugRegistry.h
/// @brief 各 RenderPass が「デバッグ表示したい中間テクスチャ(SRV)」を毎フレーム自己登録する集約点。
///
///        役割      : RenderPass 群とデバッグ UI(RenderDebugWindow) を疎結合にする。
///        責務      : {名前, SRV, 寸法, 種別} の一覧を 1 フレーム分だけ保持する。
///        位置付け  : RenderQueue と同じ流儀の singleton。BeginFrame でクリア → 各パスが Register。
///        設計方針  : ENABLE_EDITOR 時のみ実体を持ち、Release ビルドではゼロコストに畳む
///                    （Register は空 inline 化、UI 側も存在しないため一覧は決して読まれない）。
///
///        なぜ Registry か:
///          SSAO や PostProcess の中間 RT は各クラスの private。UI 側から個別 getter を
///          生やし続けると破綻するので、SRV を持っているパス自身が 1 行で自己申告する。
#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>

/// @brief バッファ中身の種別。RenderDebugWindow が表示上の注意書き（生表示の限界）に使う。
/// @note  v1 は「生 SRV をそのまま貼る」だけ。チャンネル remap / 深度線形化 / 配列スライス選択は
///        v2 の可視化シェーダーで対応予定（本 enum はその足場）。
enum class DebugTexKind : uint8_t
{
    Color,          ///< LDR/SDR カラー。そのまま正しく見える。
    HDRColor,       ///< HDR カラー。露出未適用で白飛び/黒潰れしやすい。
    Depth,          ///< 深度。非線形でほぼ白く見える（線形化は v2）。
    SingleChannel,  ///< R 成分のみ（AO 等）。赤単色で見える。
    Normal,         ///< 法線。[-1,1]→[0,1] 変換前で暗く見える。
    Array,          ///< Texture2DArray（カスケードシャドウ等）。生 SRV では表示不可（v2 でスライス選択）。
};

/// @brief デバッグ表示 1 件分の情報。SRV は「そのフレームで RTV バインドを外した後」のものを渡すこと。
struct DebugTexEntry
{
    const char*               name   = nullptr; ///< 静的文字列を想定（フレームを跨いで安全な寿命のもの）。
    ID3D11ShaderResourceView* srv    = nullptr;
    uint32_t                  width  = 0;
    uint32_t                  height = 0;
    DebugTexKind              kind   = DebugTexKind::Color;
};

/// @brief デバッグ表示用テクスチャの一覧を 1 フレーム保持する singleton。
/// @note  BeginFrame() は RenderQueue::BeginFrame() と同じ場所で呼ぶ（フレーム先頭でクリア）。
class RenderDebugRegistry
{
public:
    static RenderDebugRegistry& Instance()
    {
        static RenderDebugRegistry s;
        return s;
    }

    /// @brief フレーム先頭で一覧をクリアする（容量は再利用するので確保は起きない）。
    void BeginFrame() noexcept { m_entries.clear(); }

    /// @brief 中間テクスチャを 1 件登録する。Release ビルドでは何もしない（ゼロコスト）。
    void Register(const DebugTexEntry& e)
    {
#ifdef ENABLE_EDITOR
        if (!e.srv) return;
        m_entries.push_back(e);
#else
        (void)e;
#endif
    }

    /// @brief 登録済み一覧を取得する（RenderDebugWindow が読む）。
    const std::vector<DebugTexEntry>& GetEntries() const noexcept { return m_entries; }

private:
    RenderDebugRegistry() { m_entries.reserve(16); }

    std::vector<DebugTexEntry> m_entries;
};
