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

    // フレーム開始時にリストを空にする（メモリ再確保を防ぐため clear のみ）
    void BeginFrame() {
        _opaqueCommands.clear();
        _transparentCommands.clear();
        _sortKeys.clear();
    }

    // ECS(ウェイター)から注文を受け取る
    void SubmitOpaque(const RenderCommand& cmd) {
        _opaqueCommands.push_back(cmd);
    }

    void SubmitTransparent(const TransparencyCommand& cmd) {
        _transparentCommands.push_back(cmd);
    }

    // 描画直前に、溜まった伝票を一気にソートしてバッチを構築する
    void SortAndBuildBatches() {
        _sortKeys.clear();
        _sortKeys.reserve(_opaqueCommands.size());

        for (uint32_t i = 0; i < _opaqueCommands.size(); ++i) {
            const auto& cmd = _opaqueCommands[i];

            // 1. シェーダーソートID (8bit) - Registryから確実な連番を取得
            uint64_t shaderPart = static_cast<uint64_t>(ShaderRegistry::Instance().GetSortID(cmd.shaderHash)) & 0xFF;

            // 2. モデルアドレス (32bit) - アライメントの余分な下位4bitを捨てる
            uint64_t modelPart = (reinterpret_cast<uint64_t>(cmd.model) >> 4) & 0xFFFFFFFF;

            // 3. マテリアル情報 (24bit)
            uint64_t matPart = 0;
            if (cmd.overrideCount > 0 && cmd.overrideMaterials[0].IsValid()) {
                matPart = cmd.overrideMaterials[0].index & 0xFFFFFF;
            }

            // 魔法の合成（シフト演算とORによるガッチャンコ）
            uint64_t finalKey = (shaderPart << 56) | (modelPart << 24) | matPart;

            _sortKeys.push_back({ finalKey, i });
        }

        // 重いコマンド本体には一切触れず、8バイトのキー配列だけを光の速さでソートする
        std::sort(_sortKeys.begin(), _sortKeys.end());
    }

    // シェフ(ModelRenderer)が読み取るためのゲッター
    const std::vector<RenderCommand>& GetOpaqueCommands() const { return _opaqueCommands; }
    const std::vector<RenderKey>& GetSortedKeys() const { return _sortKeys; }
    const std::vector<TransparencyCommand>& GetTransparentCommands() const { return _transparentCommands; }

private:
    std::vector<RenderCommand>       _opaqueCommands;
    std::vector<RenderKey>           _sortKeys;
    std::vector<TransparencyCommand> _transparentCommands;
};