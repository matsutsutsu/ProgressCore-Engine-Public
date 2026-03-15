#pragma once
#include "BulletBlueprint.h" // 定義が必要
#include "ECS/Common/CCL_Common.h"
#include <array>

// ===========================================================================
// BulletContextComponent
// 役割: 弾丸1発ごとの「状態」。
// 特徴: メモリ確保(new)を一切行わない「Plain Old Data (POD)」にする。
// ===========================================================================

// ★最適化: ヒット履歴用の固定長バッファ
// 弾幕ゲーで1発が何十回も当たることは稀なので、固定サイズで十分です。
// これにより std::vector のオーバーヘッドを完全になくします。
struct FixedHitHistory {
    static constexpr int                        MAX_HISTORY = 8;
    std::array<CCL::ECS::EntityID, MAX_HISTORY> data;
    int                                         count = 0;

    void Add(CCL::ECS::EntityID id)
    {
        // リングバッファ形式で追加（古い履歴を上書き）
        // または上限で止めるかは仕様次第ですが、ここではリングバッファにします
        int index   = count % MAX_HISTORY;
        data[index] = id;
        count++;
    }

    bool Contains(CCL::ECS::EntityID id) const
    {
        int checkLimit = (count < MAX_HISTORY) ? count : MAX_HISTORY;
        for (int i = 0; i < checkLimit; ++i) {
            if (data[i] == id) return true;
        }
        return false;
    }

    void Clear() { count = 0; }
};

struct BulletContextComponent {
    // -----------------------------------------------------------------------
    // ■ 共有データへの参照 (Flyweight)
    // -----------------------------------------------------------------------
    // ここを見るだけで、アタッチメント構成や基本性能が全て分かります。
    const BulletBlueprint *blueprint = nullptr;

    // -----------------------------------------------------------------------
    // ■ 個別状態 (Mutable)
    // -----------------------------------------------------------------------
    float lifeTime      = 0.0f; // 現在の残り寿命
    float currentDamage = 0.0f; // 現在のダメージ (距離減衰などで変動可能)

    // クリティカル状態
    // 弾が生成された瞬間、または命中した瞬間に判定結果を保持します
    bool isCritical = false;

    // 残り回数管理 (BlueprintのCountからコピーされ、消費されていく)
    int pierceCount  = 0; // 残り貫通回数
    int reflectCount = 0; // 残り反射回数

    // 状態フラグ
    bool markForEnd  = false; // 死亡フラグ
    bool endExecuted = false; // End処理済みフラグ（二重爆発防止）

    // 識別情報
    CCL::ECS::EntityID selfID       = CCL::ECS::InvalidEntityID;

    // ★最適化: 固定長ヒット履歴
    FixedHitHistory hitHistory;

};