#pragma once
#include <vector>
#include <array>
#include <algorithm>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"

// ---------------------------------------------------------
// 伝票（重いデータ）
// ---------------------------------------------------------
struct RenderCommand {
    uint32_t                shaderHash;
    uint32_t                assetHash;
    const Model* model;
    DirectX::XMFLOAT4X4     worldMatrix;
    std::array<MaterialHandle, 4> overrideMaterials;
    uint8_t                 overrideCount = 0;
    DirectX::XMFLOAT4       customParams;
};

// 半透明用の伝票（距離ソートが必要なため構造が少し違う）
struct TransparencyCommand {
    uint32_t                shaderHash;
    const Model::Mesh* mesh;
    float                   distance;
    DirectX::XMFLOAT4X4     worldMatrix;
    MaterialHandle          overrideMaterial;
    DirectX::XMFLOAT4       customParams;
};

// シャドウマップ用の伝票（さらにシンプル）
struct ShadowCommand {
    const Model* model;
    const Model::Mesh* mesh;
    DirectX::XMFLOAT4X4 worldMatrix;

    // どのカスケードで描画するかを示すビットマスク (例: 001, 010, 111 等)
    uint8_t cascadeMask = 0;
};


// ---------------------------------------------------------
// ソートキー（整理券：わずか8バイトで超軽量、キャッシュ効率極大）
// ---------------------------------------------------------
struct RenderKey {
    uint64_t value;
    uint32_t originalIndex;
    bool operator<(const RenderKey& rhs) const { return value < rhs.value; }
};


// ---------------------------------------------------------
// カウンター本体（シングルトンでシステム全体からアクセス可能に）
// ---------------------------------------------------------
class RenderQueue {
public:
    static RenderQueue& Instance() { static RenderQueue instance; return instance; }

    /**
    * @brief ShaderRegistry を注入する。Graphics::Initialize から 1 回だけ呼ぶ。
    * @details これにより SortAndBuildBatches が ShaderRegistry::Instance() を
    *          叩かなくなり、ホットパスの singleton 依存がゼロになる。
    */
    void Initialize(ShaderRegistry* shaders) { _shaderRegistry = shaders; }


    // フレーム開始時にリストを空にする（メモリ再確保を防ぐため clear のみ）
    void BeginFrame() {
        _opaqueCommands.clear();
        _deferredTransparentCommands.clear();
        _shadowCommands.clear();
        _sortKeys.clear();
    }

    // ECS(ウェイター)から注文を受け取る
    void SubmitOpaque(const RenderCommand& cmd) {
        _opaqueCommands.push_back(cmd);
    }

    void SubmitDeferredTransparent(const TransparencyCommand& cmd) {
        _deferredTransparentCommands.push_back(cmd);
    }

    void SubmitShadow(const ShadowCommand& cmd) {
        _shadowCommands.push_back(cmd);
    }

    /**
     * @brief コマンドをソートしてバッチを構築する。
     * @details Initialize で注入された ShaderRegistry を使用する（runtime singleton ゼロ）。
     *          Initialize が未呼び出しの場合は shaderPart が常に 0 になる（安全フォールバック）。
     */
    void SortAndBuildBatches()
    {
        _sortKeys.clear();
        _sortKeys.reserve(_opaqueCommands.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(_opaqueCommands.size()); ++i) {
            const auto& cmd = _opaqueCommands[i];

            // (I) ShaderRegistry::Instance() を廃止 → 注入済みポインタを使用
            uint64_t shaderPart = (_shaderRegistry != nullptr)
                ? static_cast<uint64_t>(_shaderRegistry->GetSortID(cmd.shaderHash)) & 0xFF
                : 0;

            uint64_t modelPart = static_cast<uint64_t>(cmd.assetHash) & 0xFFFFFFFF;
            uint64_t matPart = 0;
            if (cmd.overrideCount > 0 && cmd.overrideMaterials[0].IsValid())
                matPart = cmd.overrideMaterials[0].index & 0xFFFFFF;

            _sortKeys.push_back({ (shaderPart << 56) | (modelPart << 24) | matPart, i });
        }
        std::sort(_sortKeys.begin(), _sortKeys.end());
    }

    // シェフ(ModelRenderer)が読み取るためのゲッター
    const std::vector<RenderCommand>& GetOpaqueCommands() const { return _opaqueCommands; }
    const std::vector<RenderKey>& GetSortedKeys() const { return _sortKeys; }
    const std::vector<TransparencyCommand>& GetDeferredTransparentCommands() const { return _deferredTransparentCommands; }
    // 距離ソートを TransparentPass 側でその場実行するための可変参照。
    // このリストは毎フレーム BeginFrame でクリア＆OpaquePass が再構築するため破壊的並べ替えで問題ない。
    std::vector<TransparencyCommand>& GetDeferredTransparentCommandsMutable() { return _deferredTransparentCommands; }
    const std::vector<ShadowCommand>& GetShadowCommands() const { return _shadowCommands; }


private:
    ShaderRegistry* _shaderRegistry = nullptr;

    std::vector<RenderCommand>       _opaqueCommands;
    std::vector<RenderKey>           _sortKeys;
    std::vector<TransparencyCommand> _deferredTransparentCommands; // 不透明パスで弾かれた半透明
    std::vector<ShadowCommand>       _shadowCommands;
};