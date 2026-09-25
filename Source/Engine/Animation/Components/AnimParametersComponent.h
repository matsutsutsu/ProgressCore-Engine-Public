#pragma once
// ===========================================================================
// 概要: アニメーションの遷移条件が参照するパラメータを持つコンポーネント AnimParametersComponent。
//
// 責務:
//   - Float / Bool / Trigger のパラメータを名前ハッシュで保持する
//   - Trigger の先行入力の寿命を管理する
//
// 備考: 書くのはゲーム側や CharacterAnimSyncSystem、読むのは AnimControllerSystem。
//       件数が少ないので map ではなく vector の線形探索で持つ。
// ===========================================================================
#include <vector>
#include <cstdint>
#include <algorithm>

struct AnimParametersComponent
{
    struct FloatParam
    {
        uint32_t hash;
        float    value;
    };
    struct BoolParam
    {
        uint32_t hash;
        bool     value;
    };

    // 寿命（先行入力の猶予時間）付きのトリガー
    struct TriggerParam
    {
        uint32_t hash;
        float    lifetime;
    };

    std::vector<FloatParam>   floats;
    std::vector<BoolParam>    bools;
    std::vector<TriggerParam> triggers;

    void SetFloat(uint32_t hash, float val)
    {
        for (auto &p : floats)
        {
            if (p.hash == hash)
            {
                p.value = val;
                return;
            }
        }
        floats.push_back({hash, val});
    }

    float GetFloat(uint32_t hash) const
    {
        for (const auto &p : floats)
        {
            if (p.hash == hash)
                return p.value;
        }
        return 0.0f; // 未登録なら 0
    }

    void SetBool(uint32_t hash, bool val)
    {
        for (auto &p : bools)
        {
            if (p.hash == hash)
            {
                p.value = val;
                return;
            }
        }
        bools.push_back({hash, val});
    }

    bool GetBool(uint32_t hash) const
    {
        for (const auto &p : bools)
        {
            if (p.hash == hash)
                return p.value;
        }
        return false;
    }

    /**
     * @brief トリガーを立てる（先行入力として登録する）。
     * @param hash       トリガーのハッシュ
     * @param bufferTime 先行入力の猶予時間（秒）。既定 0.15 秒（60fps で約 9 フレーム）
     * @note 押した瞬間を bufferTime だけ残しておくのが役割で、遷移してよいタイミングは決めない。
     *       遷移が通るのは、寿命が残っていて、かつシーケンサの ComboWindow / CancelWindow の
     *       区間内にいるときだけ（AnimControllerSystem の ConditionsMet が両方を見る）。
     *       早すぎる入力を待たせるのがここ、弾くのが窓の側。
     */
    void SetTrigger(uint32_t hash, float bufferTime = 0.15f)
    {
        for (auto &t : triggers)
        {
            if (t.hash == hash)
            {
                // 既にあれば寿命を上書きする
                t.lifetime = bufferTime;
                return;
            }
        }
        triggers.push_back({hash, bufferTime});
    }

    bool HasTrigger(uint32_t hash) const
    {
        for (const auto &t : triggers)
        {
            if (t.hash == hash)
                return true;
        }
        return false;
    }

    void ConsumeTrigger(uint32_t hash)
    {
        triggers.erase(
            std::remove_if(triggers.begin(), triggers.end(),
                           [hash](const TriggerParam &t) { return t.hash == hash; }),
            triggers.end());
    }

    // 寿命を dt だけ減らし、切れたトリガーを消す（AnimControllerSystem が毎ステップ呼ぶ）
    void TickTriggers(float dt)
    {
        if (triggers.empty())
            return;

        for (auto &t : triggers)
        {
            t.lifetime -= dt;
        }

        // 寿命が 0 以下のトリガーをまとめて削除する（erase-remove）
        triggers.erase(
            std::remove_if(triggers.begin(), triggers.end(),
                           [](const TriggerParam &t) { return t.lifetime <= 0.0f; }),
            triggers.end());
    }

    // Float か Bool（1/0 として）の値を取り出す。どちらにも無ければ false
    bool TryGetNumeric(uint32_t hash, float &outValue) const
    {
        for (const auto &p : floats)
        {
            if (p.hash == hash)
            {
                outValue = p.value;
                return true;
            }
        }
        for (const auto &p : bools)
        {
            if (p.hash == hash)
            {
                outValue = p.value ? 1.0f : 0.0f;
                return true;
            }
        }
        return false;
    }

    // トリガーを全て消す。被弾・回避・死亡など、それまでの先行入力を無効にしたいときに呼ぶ
    void ClearTriggers()
    {
        triggers.clear();
    }
};