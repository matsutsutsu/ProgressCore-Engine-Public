#pragma once
// ===========================================================================
// 概要: 演出からの一時的な上書きを、演出ごとの枠で受け取る小さな入れ物
//
// 備考: 演出が設定を直接書き換えると、重なったときに後から書いた方が勝ち、戻し方次第でシーンの設定ごと壊れる
//       そこで各演出は自分の枠だけを触り、合成は値を読む側（PostProcessSystem など）が1か所で行う
//       枠の持ち主は演出の名前のハッシュで区別するので、演出を足してもエンジンのヘッダは変わらない
//
// 使い方:
//   static const uint32_t kSource = CCL::Utils::HashString("MyEffect");
//
//   // 出したいフレーム（毎フレーム呼んでよい）
//   if (auto *ov = vignette->overrides.Acquire(kSource, kPriority, vignette->BaseParams()))
//   {
//       ov->weight           = env;  // 普段の設定からどれだけ寄せるか（0〜1）
//       ov->params.intensity = peak; // 変えたい項目だけ書く
//   }
//
//   // 終わったフレーム（確保していなければ何もしないので、毎フレーム呼んでよい）
//   vignette->overrides.Release(kSource);
// ===========================================================================
#include "Engine/Platform/Core/Logger.h"

#include <algorithm>
#include <cstdint>

// TParams 型の上書きを、最大 N 個まで持つ
template <class TParams, int N = 4>
struct PostEffectOverrides
{
    // 演出1つ分の枠
    struct Slot
    {
        uint32_t source   = 0;    // 持ち主の演出の名前のハッシュ。0 は空き
        int      priority = 0;    // 大きいほど後に重なる（手前に出る）
        float    weight   = 0.0f; // 普段の設定から params へ寄せる割合（0〜1）。0 で効かない
        TParams  params{};        // 寄せる先の目標値

        // 合成に使う効き。weight を 1 で頭打ちにしたもの
        float Amount() const { return (std::min)(weight, 1.0f); }
    };

    // 効いている枠を priority の小さい順に並べたもの。範囲 for で回せる
    struct ActiveList
    {
        const Slot *items[N] = {}; // 所有しない
        int         count    = 0;

        const Slot *const *begin() const { return items; }
        const Slot *const *end() const { return items + count; }
    };

    Slot slots[N];

    // source の枠を返す。まだなければ空きを1つ確保し、params に base を入れる
    // 同じ source なら同じ枠が返るので、毎フレーム呼んでよい。満杯なら nullptr（その演出は出ない）
    Slot *Acquire(uint32_t source, int priority, const TParams &base = TParams{})
    {
        if (source == 0) return nullptr;

        // 持っている枠はそのまま返す
        // base を入れ直すと、前のフレームに書いた値が毎フレーム消える
        for (Slot &s : slots)
        {
            if (s.source != source) continue;
            s.priority = priority;
            return &s;
        }

        for (Slot &s : slots)
        {
            if (s.source != 0) continue;
            s = Slot{source, priority, 0.0f, base};
            return &s;
        }

        // 満杯は「演出が何も言わずに出ない」という追いにくい症状になるので、ログに残す
        CCL_LOG_WARN(LogCategory::Render, "[PostEffectOverrides] 枠が満杯で確保できなかった（容量 %d）。この演出は出ない", N);
        return nullptr;
    }

    // source の枠を空ける。演出が終わったときに呼ぶ（確保していなければ何もしない）
    void Release(uint32_t source)
    {
        for (Slot &s : slots)
        {
            if (s.source != source) continue;
            s = Slot{};
            return;
        }
    }

    // 効いている枠が1つでもあるか（普段の設定がオフでも描くかの判定に使う）
    bool AnyActive() const
    {
        for (const Slot &s : slots)
            if (s.source != 0 && s.weight > 0.0f) return true;
        return false;
    }

    // 効いている枠を priority の小さい順に返す
    // 重ねて合成すると順番で結果が変わるので、枠の並びではなく priority で決める
    ActiveList SortedActive() const
    {
        ActiveList list;
        for (const Slot &s : slots)
            if (s.source != 0 && s.weight > 0.0f) list.items[list.count++] = &s;

        std::stable_sort(list.items, list.items + list.count,
                         [](const Slot *a, const Slot *b) { return a->priority < b->priority; });
        return list;
    }
};
