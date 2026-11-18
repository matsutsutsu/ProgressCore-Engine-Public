#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>

// アニメの遷移条件が見るパラメータ（Float / Bool / Trigger）を名前ハッシュで持つ
// 書くのはゲーム側と CharacterAnimSyncSystem、読むのは AnimControllerSystem
// 件数が少ないので、map ではなく vector の線形探索で引く
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
        float    lifetime; // 残りの寿命（秒）
    };

    std::vector<FloatParam>   floats;
    std::vector<BoolParam>    bools;
    std::vector<TriggerParam> triggers;

    void SetFloat(uint32_t hash, float val)
    {
        if (FloatParam *p = Find(floats, hash)) p->value = val;
        else floats.push_back({hash, val});
    }

    void SetBool(uint32_t hash, bool val)
    {
        if (BoolParam *p = Find(bools, hash)) p->value = val;
        else bools.push_back({hash, val});
    }

    bool GetBool(uint32_t hash) const
    {
        const BoolParam *p = Find(bools, hash);
        return p ? p->value : false;
    }

    // トリガーを先行入力として立てる。すでにあれば寿命を上書きする。bufferTime は猶予（秒）
    // 遷移してよいタイミングは決めない。それは遷移の側の受付区間（requiredCancelMask）が決める
    void SetTrigger(uint32_t hash, float bufferTime = 0.15f)
    {
        if (TriggerParam *t = Find(triggers, hash)) t->lifetime = bufferTime;
        else triggers.push_back({hash, bufferTime});
    }

    bool HasTrigger(uint32_t hash) const { return Find(triggers, hash) != nullptr; }

    // hash のトリガーを消す（遷移で使ったトリガーを、寿命を待たずに下ろす）
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
        if (triggers.empty()) return;

        for (auto &t : triggers) t.lifetime -= dt;

        triggers.erase(
            std::remove_if(triggers.begin(), triggers.end(),
                           [](const TriggerParam &t) { return t.lifetime <= 0.0f; }),
            triggers.end());
    }

    // Float か Bool（1/0 として）の値を取り出す。どちらにもなければ false
    bool TryGetNumeric(uint32_t hash, float &outValue) const
    {
        if (const FloatParam *p = Find(floats, hash))
        {
            outValue = p->value;
            return true;
        }
        if (const BoolParam *p = Find(bools, hash))
        {
            outValue = p->value ? 1.0f : 0.0f;
            return true;
        }
        return false;
    }

private:
    // params（floats / bools / triggers）から hash の要素を探す。なければ nullptr
    template <class Params>
    static auto Find(Params &params, uint32_t hash) -> decltype(params.data())
    {
        for (auto &p : params)
            if (p.hash == hash) return &p;
        return nullptr;
    }
};
