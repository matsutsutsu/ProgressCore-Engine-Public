#pragma once
// ===========================================================================
// 概要: 描画システムが積んだ描画コマンドを1フレーム分ため、RenderPass に渡すキュー。
//
// 責務:
//   - 不透明・半透明・影のコマンドをためる（ModelRenderSystem・OpaquePass・ShadowMapRenderSystem が積む）
//   - 不透明のコマンドを シェーダー → モデル → マテリアル の順のキーで並べる
//   - NormalPrepass が求めた見えるインスタンスの列を、OpaquePass に渡す
//
// 備考: BaseScene がフレームの最初に BeginFrame を呼んで空にする。
//       並べるのは 8 バイトのキーと元の添字だけで、コマンド本体は動かさない。
// ===========================================================================
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h" // InstanceData

// 不透明のモデル1体分の描画コマンド
struct RenderCommand
{
    uint32_t                      shaderHash;        // 1つ目のマテリアルのシェーダー
    uint32_t                      assetHash;         // モデルのパスのハッシュ（同じモデルをまとめるのに使う）
    const Model                  *model;             // 所有しない
    DirectX::XMFLOAT4X4           worldMatrix;       // エンティティのワールド行列
    std::array<MaterialHandle, 4> overrideMaterials; // メッシュごとの上書きのマテリアル（先頭の4つまで）
    uint8_t                       overrideCount = 0; // overrideMaterials のうち使っている数
    DirectX::XMFLOAT4             customParams;      // InstanceData::customParams に入れる値
};

// 半透明のメッシュ1つ分の描画コマンド（OpaquePass が積む。カメラからの距離で並べる）
struct TransparencyCommand
{
    uint32_t            shaderHash;
    const Model::Mesh  *mesh;             // 所有しない
    float               distance;         // カメラからの距離（m）
    DirectX::XMFLOAT4X4 worldMatrix;      // ノードの行列まで掛けたもの（スキンメッシュはエンティティのまま）
    MaterialHandle      overrideMaterial; // 無効なら mesh.material を使う
    DirectX::XMFLOAT4   customParams;
};

// 影を落とすメッシュ1つ分の描画コマンド
struct ShadowCommand
{
    const Model        *model;       // 所有しない（今は ShadowPass が読んでいない）
    const Model::Mesh  *mesh;        // 所有しない
    DirectX::XMFLOAT4X4 worldMatrix; // エンティティのワールド行列

    uint8_t cascadeMask = 0; // 描くカスケードのビット（bit i = カスケード i）
};

// 並べ替え用のキー。value の上位から シェーダーの並び順（8bit）・モデル（32bit）・マテリアル（24bit）
struct RenderKey
{
    uint64_t value;
    uint32_t originalIndex; // RenderQueue::m_opaqueCommands の添字
    bool     operator<(const RenderKey &rhs) const { return value < rhs.value; }
};

class RenderQueue
{
public:
    static RenderQueue &Instance()
    {
        static RenderQueue instance;
        return instance;
    }

    // 並べ替えで使う ShaderRegistry を渡す（Graphics の初期化で1回だけ呼ぶ）。
    // 毎フレームの並べ替えで ShaderRegistry::Instance() を引かないために持っておく
    void Initialize(ShaderRegistry *shaders) { m_shaderRegistry = shaders; }

    // 全部の配列を空にする（容量は残すので、毎フレームのメモリ確保は起きない）
    void BeginFrame()
    {
        m_opaqueCommands.clear();
        m_deferredTransparentCommands.clear();
        m_shadowCommands.clear();
        m_sortKeys.clear();
        m_batchesBuilt = false;

        m_visibleArena.clear();
        m_visibleIndex.clear();
    }

    // ============================================================
    // メッシュごとの見えるインスタンスの列（NormalPrepass → OpaquePass）
    // ============================================================
    //   NormalPrepass と OpaquePass は、同じ並べたキーの列を同じバッチの区切りで回し、
    //   同じ MeshCull でカリングするので、(バッチ, メッシュ) ごとの見えるインスタンスの列は一致する。
    //   先に走る NormalPrepass の結果をここに置き、OpaquePass が読んで計算を省く。
    //
    //   書くのは NormalPrepass だけ、読むのは OpaquePass だけにしている。OpaquePass が
    //   書き足さないので、読んでいる間に m_visibleArena が確保し直されて、返したポインタが
    //   無効になることは無い。
    //
    //   見つからなければ OpaquePass が自分で計算する。NormalPrepass はブレンドありのバッチを
    //   飛ばし、パス自体が走らないこともある（シェーダーが取れない・enabled が false）ので、
    //   見つからないのは異常ではない。

    // (バッチの先頭の添字, メッシュの番号) からキーを作る
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

    // 見つかれば outData / outCount に入れて true。count == 0（全部カリングされた）も見つかった扱い
    bool FindVisibleInstances(uint64_t key, const InstanceData *&outData, size_t &outCount) const
    {
        const auto it = m_visibleIndex.find(key);
        if (it == m_visibleIndex.end()) return false;
        outData  = m_visibleArena.data() + it->second.offset;
        outCount = it->second.count;
        return true;
    }

    // ============================================================
    // コマンドを積む
    // ============================================================
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
     * @brief 不透明のコマンドのキーを作って並べる。
     *
     * そのフレームで最初に呼んだパス（NormalPrepass か OpaquePass）だけが並べ、
     * 2回目からは何もしない（同じ m_sortKeys を使う）。
     *
     * @note Initialize が呼ばれていなければ、シェーダーの部分は常に 0 になる。
     * @note マテリアルの部分は、上書きのマテリアルの組み合わせ（overrideMaterials の全部）ごとの番号。
     *       組み合わせが1つでも違えば別のバッチになる。
     */
    void SortAndBuildBatches()
    {
        if (m_batchesBuilt) return;

        m_sortKeys.clear();
        m_sortKeys.reserve(m_opaqueCommands.size());
        m_overrideSets.clear();

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_opaqueCommands.size()); ++i)
        {
            const auto &cmd = m_opaqueCommands[i];

            uint64_t shaderPart = (m_shaderRegistry != nullptr)
                                      ? static_cast<uint64_t>(m_shaderRegistry->GetSortID(cmd.shaderHash)) & 0xFF
                                      : 0;

            uint64_t modelPart = static_cast<uint64_t>(cmd.assetHash) & 0xFFFFFFFF;
            uint64_t matPart   = static_cast<uint64_t>(GetOverrideSetID(i)) & 0xFFFFFF;

            m_sortKeys.push_back({(shaderPart << 56) | (modelPart << 24) | matPart, i});
        }
        std::sort(m_sortKeys.begin(), m_sortKeys.end());
        m_batchesBuilt = true;
    }

    const std::vector<RenderCommand>       &GetOpaqueCommands() const { return m_opaqueCommands; }
    const std::vector<RenderKey>           &GetSortedKeys() const { return m_sortKeys; }
    const std::vector<TransparencyCommand> &GetDeferredTransparentCommands() const { return m_deferredTransparentCommands; }
    // TransparentPass がその場で距離順に並べ替えるための参照（毎フレーム作り直すので壊してよい）
    std::vector<TransparencyCommand> &GetDeferredTransparentCommandsMutable() { return m_deferredTransparentCommands; }
    const std::vector<ShadowCommand> &GetShadowCommands() const { return m_shadowCommands; }

private:
    // 見えるインスタンスの列を詰めた配列と、キー → 配列の区間 の表
    struct VisibleSpan
    {
        uint32_t offset;
        uint32_t count;
    };
    std::vector<InstanceData>                 m_visibleArena;
    std::unordered_map<uint64_t, VisibleSpan> m_visibleIndex;

    ShaderRegistry *m_shaderRegistry = nullptr; // 所有しない

    bool m_batchesBuilt = false; // このフレームで SortAndBuildBatches が済んだか

    // このフレームで見つけた上書きのマテリアルの組み合わせ（それを最初に使ったコマンドの添字）
    std::vector<uint32_t> m_overrideSets;

    /**
     * @brief 上書きのマテリアルの組み合わせに、このフレームだけの番号を振る。
     *
     * 組み合わせは普通は数種類しかないので、線形に探す。
     *
     * @param commandIndex 番号を振る描画コマンドの添字。
     * @return 組み合わせの番号。組み合わせが同じなら同じ番号、上書きが無ければ 0。
     */
    uint32_t GetOverrideSetID(uint32_t commandIndex)
    {
        const RenderCommand &cmd = m_opaqueCommands[commandIndex];
        if (cmd.overrideCount == 0) return 0;

        for (size_t i = 0; i < m_overrideSets.size(); ++i)
        {
            const RenderCommand &other = m_opaqueCommands[m_overrideSets[i]];
            if (other.overrideCount == cmd.overrideCount &&
                std::equal(cmd.overrideMaterials.begin(), cmd.overrideMaterials.begin() + cmd.overrideCount,
                           other.overrideMaterials.begin()))
            {
                return static_cast<uint32_t>(i) + 1;
            }
        }
        m_overrideSets.push_back(commandIndex);
        return static_cast<uint32_t>(m_overrideSets.size());
    }

    std::vector<RenderCommand>       m_opaqueCommands;
    std::vector<RenderKey>           m_sortKeys;
    std::vector<TransparencyCommand> m_deferredTransparentCommands; // OpaquePass が回した半透明
    std::vector<ShadowCommand>       m_shadowCommands;
};
