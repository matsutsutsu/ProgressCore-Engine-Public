#pragma once
// ===========================================================================
// 概要: 画面左下（HP ゲージの上）に切り払いのストックを◆の粒で作り、CleaveComponent に毎フレーム合わせる System。
//
// 責務:
//   - Initialize で見出しと、ストック1つにつき◆1粒（縁・地・中身・光）の UI エンティティを作る
//   - たまったストックは中身を満たし、次にたまる1粒だけ中身をたまり具合に応じて中心から広げる
//   - ストックが増えた瞬間に、その粒を光らせる
//   - CleaveComponent を持つエンティティが居なければ隠す
//
// 備考: 位置は PlayerHealthHudSystem のゲージの上に乗るよう、向こうの配置の定数と合わせている。
//       ◆は正方形の画像を 45 度回して作る（UI の回転は矩形の中心まわり）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h"

#include <DirectXMath.h>
#include <array>

class CleaveGaugeHudSystem : public CCL::ECS::IfSystem<CleaveGaugeHudSystem,
                                                       CCL::ECS::Read<CleaveComponent>>
{
public:
    CleaveGaugeHudSystem()
        : IfSystem("CleaveGaugeHudSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

private:
    // 粒の数。CleaveComponent::maxStock の既定（3）に合わせる。maxStock がこれより多ければ、超えた分は出さない
    static constexpr int kPipCount = 3;

    // ◆1粒を作る UI エンティティ
    struct Pip
    {
        CCL::ECS::EntityID rim   = CCL::ECS::InvalidEntityID; // 縁（地より一回り大きい淡い◆。明るい背景でも形が見えるように）
        CCL::ECS::EntityID bg    = CCL::ECS::InvalidEntityID; // 地（空のときに見える暗い◆）
        CCL::ECS::EntityID fill  = CCL::ECS::InvalidEntityID; // 中身（たまり具合で大きさが変わる）
        CCL::ECS::EntityID flash = CCL::ECS::InvalidEntityID; // ストックが増えた瞬間の光（加算合成）
    };

    /**
     * @brief ◆1つ分の UI エンティティ（回した正方形）を作る。
     * @param canvas    親にするキャンバス。
     * @param name      エンティティ名。
     * @param centerX   ◆の中心の横位置（px）。縦位置はゲージの行で固定。
     * @param size      ◆の一辺（px）。
     * @param color     ◆の色。
     * @param sortOrder 描画順。大きいほど手前。
     * @param additive  加算合成で描くか（光るフラッシュの層だけ true）。
     * @return 作ったエンティティ。
     */
    CCL::ECS::EntityID SpawnDiamond(CCL::ECS::EntityID canvas, const char *name, float centerX, float size,
                                    const DirectX::XMFLOAT4 &color, int sortOrder, bool additive);

    // 1粒の4枚（縁・地・中身・光）をまとめて隠す・出す
    void SetPipVisible(const Pip &pip, bool visible);

    std::array<Pip, kPipCount>   m_pips{};                                // 粒ごとの UI エンティティ
    std::array<float, kPipCount> m_flash{};                               // 粒ごとの光の残り（1→0。毎フレーム減らす）
    CCL::ECS::EntityID           m_labelID   = CCL::ECS::InvalidEntityID; // 見出し（UIText）
    int                          m_prevStock = -1;                        // 前のフレームのストック数（-1 は未初期化。増えた瞬間を見るため）

    // --- 調整値 ---
    static constexpr float kFlashDecay = 3.5f; // 光が消える速さ（1/秒。大きいほど短い）
};
