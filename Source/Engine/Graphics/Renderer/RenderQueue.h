#pragma once
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h" // InstanceData（可視インスタンス共有用）

// ---------------------------------------------------------
// 伝票（重いデータ）
// ---------------------------------------------------------
struct RenderCommand
{
    uint32_t                      shaderHash;
    uint32_t                      assetHash;
    const Model                  *model;
    DirectX::XMFLOAT4X4           worldMatrix;
    std::array<MaterialHandle, 4> overrideMaterials;
    uint8_t                       overrideCount = 0;
    DirectX::XMFLOAT4             customParams;
};

// 半透明用の伝票（距離ソートが必要なため構造が少し違う）
struct TransparencyCommand
{
    uint32_t            shaderHash;
    const Model::Mesh  *mesh;
    float               distance;
    DirectX::XMFLOAT4X4 worldMatrix;
    MaterialHandle      overrideMaterial;
    DirectX::XMFLOAT4   customParams;
};

// シャドウマップ用の伝票（さらにシンプル）
struct ShadowCommand
{
    const Model        *model;
    const Model::Mesh  *mesh;
    DirectX::XMFLOAT4X4 worldMatrix;

    // どのカスケードで描画するかを示すビットマスク (例: 001, 010, 111 等)
    uint8_t cascadeMask = 0;
};

// ---------------------------------------------------------
// ソートキー（整理券：わずか8バイトで超軽量、キャッシュ効率極大）
// ---------------------------------------------------------
struct RenderKey
{
    uint64_t value;
    uint32_t originalIndex;
    bool     operator<(const RenderKey &rhs) const { return value < rhs.value; }
};

// ---------------------------------------------------------
// カウンター本体（シングルトンでシステム全体からアクセス可能に）
// ---------------------------------------------------------
class RenderQueue
{
public:
    static RenderQueue &Instance()
    {
        static RenderQueue instance;
        return instance;
    }

    /**
    * @brief ShaderRegistry を注入する。Graphics::Initialize から 1 回だけ呼ぶ。
    * @details これにより SortAndBuildBatches が ShaderRegistry::Instance() を
    *          叩かなくなり、ホットパスの singleton 依存がゼロになる。
    */
    void Initialize(ShaderRegistry *shaders) { m_shaderRegistry = shaders; }

    // フレーム開始時にリストを空にする（メモリ再確保を防ぐため clear のみ）
    void BeginFrame()
    {
        m_opaqueCommands.clear();
        m_deferredTransparentCommands.clear();
        m_shadowCommands.clear();
        m_sortKeys.clear();
        m_batchesBuilt = false; // このフレームのソートはまだ未構築

        m_visibleArena.clear();
        m_visibleIndex.clear();
    }

    // ---------------------------------------------------------
    // メッシュ単位の可視インスタンス列（Zプリパス → 不透明パスの受け渡し）
    //
    //   NormalPrepass と OpaquePass は「同じソート済みキー列を、同じバッチ区切りで、
    //   同じ MeshCull で」走査する。つまり各(バッチ, メッシュ)の可視インスタンス列は
    //   完全に同一なのに、両者が独立に計算していた（＝全不透明ジオメトリぶん2周）。
    //   先に走る NormalPrepass の結果をここへ置き、OpaquePass が読む。
    //
    //   書き手は NormalPrepass だけ、読み手は OpaquePass だけ、と決めてある。
    //   OpaquePass が書き足さないので、読み出し中に m_visibleArena が再確保されて
    //   返したポインタが宙に浮く、という事故が起こらない。
    //
    //   見つからなければ読み手が自分で計算する（＝キャッシュミスは常に安全）。
    //   NormalPrepass は cutout や blend 有効のバッチを飛ばすし、エディタから
    //   パス自体を無効化もできるので、ミスは正常な経路として起こりうる。
    // ---------------------------------------------------------

    // (バッチ先頭インデックス, メッシュ番号) から一意なキーを作る。
    static uint64_t MakeVisibleKey(size_t batchStart, size_t meshIndex)
    {
        return (static_cast<uint64_t>(batchStart) << 32) | static_cast<uint32_t>(meshIndex);
    }

    void StoreVisibleInstances(uint64_t key, const InstanceData *data, size_t count)
    {
        if (!data) return;
        const uint32_t offset = static_cast<uint32_t>(m_visibleArena.size());
        m_visibleArena.insert(m_visibleArena.end(), data, data + count);
        m_visibleIndex[key] = {offset, static_cast<uint32_t>(count)};
    }

    // 見つかれば outData/outCount を埋めて true。count==0（全数カリング済み）も正当なヒット。
    bool FindVisibleInstances(uint64_t key, const InstanceData *&outData, size_t &outCount) const
    {
        const auto it = m_visibleIndex.find(key);
        if (it == m_visibleIndex.end()) return false;
        outData  = m_visibleArena.data() + it->second.offset;
        outCount = it->second.count;
        return true;
    }

    // ECS(ウェイター)から注文を受け取る
    void SubmitOpaque(const RenderCommand &cmd)
    {
        m_opaqueCommands.push_back(cmd);
    }

    void SubmitDeferredTransparent(const TransparencyCommand &cmd)
    {
        m_deferredTransparentCommands.push_back(cmd);
    }

    void SubmitShadow(const ShadowCommand &cmd)
    {
        m_shadowCommands.push_back(cmd);
    }

    /**
     * @brief コマンドをソートしてバッチを構築する。
     * @details Initialize で注入された ShaderRegistry を使用する（runtime singleton ゼロ）。
     *          Initialize が未呼び出しの場合は shaderPart が常に 0 になる（安全フォールバック）。
     */
    void SortAndBuildBatches()
    {
        // フレーム内で最初に呼んだパス（Z プリパス or 不透明パス）だけが実際に構築する。
        // 以降の呼び出しは同じ m_sortKeys を再利用し、二重ソートを避ける。
        if (m_batchesBuilt) return;

        m_sortKeys.clear();
        m_sortKeys.reserve(m_opaqueCommands.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_opaqueCommands.size()); ++i)
        {
            const auto &cmd = m_opaqueCommands[i];

            // (I) ShaderRegistry::Instance() を廃止 → 注入済みポインタを使用
            uint64_t shaderPart = (m_shaderRegistry != nullptr)
                                      ? static_cast<uint64_t>(m_shaderRegistry->GetSortID(cmd.shaderHash)) & 0xFF
                                      : 0;

            uint64_t modelPart = static_cast<uint64_t>(cmd.assetHash) & 0xFFFFFFFF;
            uint64_t matPart   = 0;
            if (cmd.overrideCount > 0 && cmd.overrideMaterials[0].IsValid())
                matPart = cmd.overrideMaterials[0].index & 0xFFFFFF;

            m_sortKeys.push_back({(shaderPart << 56) | (modelPart << 24) | matPart, i});
        }
        std::sort(m_sortKeys.begin(), m_sortKeys.end());
        m_batchesBuilt = true;
    }

    // 各 RenderPass が読み取るためのゲッター
    const std::vector<RenderCommand>       &GetOpaqueCommands() const { return m_opaqueCommands; }
    const std::vector<RenderKey>           &GetSortedKeys() const { return m_sortKeys; }
    const std::vector<TransparencyCommand> &GetDeferredTransparentCommands() const { return m_deferredTransparentCommands; }
    // 距離ソートを TransparentPass 側でその場実行するための可変参照。
    // このリストは毎フレーム BeginFrame でクリア＆OpaquePass が再構築するため破壊的並べ替えで問題ない。
    std::vector<TransparencyCommand> &GetDeferredTransparentCommandsMutable() { return m_deferredTransparentCommands; }
    const std::vector<ShadowCommand> &GetShadowCommands() const { return m_shadowCommands; }

private:
    // 可視インスタンス列の実体（アリーナ）と、キー → 区間 の索引。
    // 毎フレーム clear するだけで容量は維持されるため、数フレームで確保が収束する。
    struct VisibleSpan
    {
        uint32_t offset;
        uint32_t count;
    };
    std::vector<InstanceData>                 m_visibleArena;
    std::unordered_map<uint64_t, VisibleSpan> m_visibleIndex;

    ShaderRegistry *m_shaderRegistry = nullptr;

    bool m_batchesBuilt = false; // 当該フレームで SortAndBuildBatches 済みか

    std::vector<RenderCommand>       m_opaqueCommands;
    std::vector<RenderKey>           m_sortKeys;
    std::vector<TransparencyCommand> m_deferredTransparentCommands; // 不透明パスで弾かれた半透明
    std::vector<ShadowCommand>       m_shadowCommands;
};