#pragma once

#include <vector>
#include <algorithm> // HasPierceの探索用
#include <DirectXMath.h> // ベクトル計算用
#include "Game/Logics/Combat/Weapon/Core/BulletEnums.h"
#include "ECS/Common/CCL_Common.h"

// ===================================================================================
// クラス名: GunBuilder
// 役割: 弾丸生成の「設計図」兼「工場」。ビルド画面で編集される対象。
//
// 機能:
//   - 弾の基本性能（速度、数、拡散など）の保持
//   - 各フェーズ（Spawn, Travel, OnHit, End）ごとのエフェクトIDリストの保持
//   - Fire(): 実際に弾丸Entityを生成し、コンポーネントを取り付ける処理
// ===================================================================================

// 前方宣言 (includeの代わりにこれを使うと相互参照エラーを防げます)
struct BulletBlueprint;
struct AttachmentBinding;

// Worldの中身はここでは不要。「そういうクラスがある」ことだけ伝える
namespace CCL::ECS::Core { class World; }

// アタッチメントの種類とレベルを管理する構造体
struct ModSlot {
    EffectID effect;
    int      level = 1; // デフォルトはLv1
};

class GunBuilder {
public:
    // --- 基礎パラメータ（ここを追加） ---
    int baseBulletCount = 1;    // 基本発射数
    float baseSpeed = 50.0f;    // 基本弾速
    float baseSpread = 0.0f;    // 基本拡散角度（度数法）
    float spawnOffset = 1.5f;   // 発射位置をどれだけ前にずらすか
    float baseLifeTime    = 3.0f;    // 基本寿命
    float baseDamage      = 10.0f;   // 基本ダメージ

    // ★追加: クリティカル・ノックバック設定
    float criticalChance     = 0.1f; // 10%
    float criticalMultiplier = 2.0f; // 2倍
    float knockbackPower     = 5.0f; // 吹き飛ばし力

    // --- アタッチメントスロット上限数 ---
    // 初期値は「3」
    int maxSpawnSlots  = 3;
    int maxTravelSlots = 3;
    int maxOnHitSlots  = 3;
    int maxEndSlots    = 3;

    // ビルド画面でプレイヤーが編集するデータ
    std::vector<ModSlot> spawnMods;
    std::vector<ModSlot> travelMods;
    std::vector<ModSlot> onHitMods;
    std::vector<ModSlot> endMods;

    // --- 枠解放メソッド ---
    void UnlockSlot(BulletPhase phase)
    {
        switch (phase) {
        case BulletPhase::Spawn: maxSpawnSlots++; break;
        case BulletPhase::Travel: maxTravelSlots++; break;
        case BulletPhase::OnHit: maxOnHitSlots++; break;
        case BulletPhase::End: maxEndSlots++; break;
        default: break;
        }
    }

    // --- 空きがあるかチェックするメソッド（★追加） ---
    bool CanAddMod(BulletPhase phase) const
    {
        switch (phase) {
        case BulletPhase::Spawn: return (int)spawnMods.size() < maxSpawnSlots;
        case BulletPhase::Travel: return (int)travelMods.size() < maxTravelSlots;
        case BulletPhase::OnHit: return (int)onHitMods.size() < maxOnHitSlots;
        case BulletPhase::End: return (int)endMods.size() < maxEndSlots;
        default: return false;
        }
    }

    void Fire(CCL::ECS::Core::World* world, DirectX::XMFLOAT3 startPos, DirectX::XMFLOAT3 direction);

    void EnemyFire(CCL::ECS::Core::World* world, DirectX::XMFLOAT3 startPos, DirectX::XMFLOAT3 direction);

    // 構成が変更されたことを知らせる関数
    void MarkDirty() { _isDirty = true; }

  private:

    // 生成したBlueprintのキャッシュ
    // 構成が変わらない限り、これを使い回すことで毎フレームの計算を省略します
    const BulletBlueprint *_cachedBlueprint = nullptr;

    // 構成確定時に事前計算しておくキャッシュデータ
    CCL::ECS::Archetype                    _cachedArchetype;
    std::vector<const AttachmentBinding *> _activeBindings;

    // キャッシュが無効かどうかのフラグ
    // modを追加したりパラメータを変えたら true にする
    bool _isDirty = true;

    // 内部ヘルパー: 現在の設定からBlueprintを作成・登録する
    const BulletBlueprint *ResolveBlueprint(CCL::ECS::Core::World *world);

  private:
    // ---------------------------------------------------------
    // ヘルパー関数実装
    // ---------------------------------------------------------

    // 速度ベクトルの計算（扇状拡散ロジック）
    DirectX::XMFLOAT3 CalculateVelocity(DirectX::XMFLOAT3 baseDir, float speed, float totalSpreadDeg, int index, int totalCount);

};