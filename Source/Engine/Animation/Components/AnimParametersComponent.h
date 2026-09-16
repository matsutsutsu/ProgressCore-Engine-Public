#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>

// ===================================================================================
// ファイル: AnimParametersComponent.h
// 概要: アニメーション状態遷移のための「パラメータ掲示板」コンポーネント
// ===================================================================================

// CPUキャッシュラインに収まりやすいフラットなパラメータボード
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

    // トリガーに寿命(先行入力猶予時間)を持たせる
    struct TriggerParam
    {
        uint32_t hash;
        float    lifetime;
    };

    std::vector<FloatParam>   floats;
    std::vector<BoolParam>    bools;
    std::vector<TriggerParam> triggers; // IDリストから構造体にアップグレード

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
        return 0.0f; // デフォルト値
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
     * @brief トリガーを発火させる（先行入力バッファへの登録）
     * @param hash トリガーのハッシュ
     * @param bufferTime 先行入力の猶予時間（秒）。既定 0.15 秒（60fps で約9フレーム）
     * @note  「押した瞬間」を bufferTime だけ生かしておくのが役割で、遷移してよい
     *        タイミングまでは決めない。実際に遷移が通るのは、寿命が残っていることに
     *        加えてシーケンサの ComboWindow / CancelWindow の区間内にいる時だけ
     *        （AnimControllerSystem::ConditionsMet が両方を AND で見る）。
     *        早すぎる入力を「待たせる」のがここ、「弾く」のが窓側。
     */
    void SetTrigger(uint32_t hash, float bufferTime = 0.15f)
    {
        for (auto &t : triggers)
        {
            if (t.hash == hash)
            {
                // 既に存在する場合は、寿命を上書き(延長)する
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

    // 毎フレーム呼ばれ、バッファの寿命を減らし、切れたものを掃除する
    void TickTriggers(float dt)
    {
        if (triggers.empty())
            return;

        // 寿命を減らす
        for (auto &t : triggers)
        {
            t.lifetime -= dt;
        }

        // 寿命が0以下のトリガーを一括削除 (Erase-Remove Idiom)
        triggers.erase(
            std::remove_if(triggers.begin(), triggers.end(),
                           [](const TriggerParam &t) { return t.lifetime <= 0.0f; }),
            triggers.end());
    }

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

    // -----------------------------------------------------------------------
    // 暴発防止用：トリガーの全消去（フラッシュ）
    // ダメージを受けた瞬間や、回避・死亡時など、
    // 過去の入力を無効化したい「割り込み処理」が発生した際に呼び出す。
    // -----------------------------------------------------------------------
    void ClearTriggers()
    {
        triggers.clear();
    }
};