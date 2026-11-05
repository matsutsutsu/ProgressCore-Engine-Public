#pragma once
// ===========================================================================
// 概要: ポストプロセス設定への一時的な上書きを、発生源ごとに受け取る固定長のスロット
//
// 備考: 演出が設定を直接書き換えると、重なったときに後から書いた方が勝ち、戻し方次第でシーンの設定ごと壊れる
//       そこで各発生源は自分のスロットだけを持ち、合成は PostProcessSystem が1か所で行う
//       名前付きのフィールドでなくキーにするので、演出を足してもエンジンのヘッダは変わらない
//
// @code
//   static const uint32_t kSource = CCL::Utils::HashString("MyEffect");
//   if (auto *ov = comp->overrides.Acquire(kSource, 10)) { ov->weight = env; ... }
//   // 演出が終わったら
//   comp->overrides.Release(kSource);
// @endcode
// ===========================================================================
#include <cstdint>

// TParams 型の上書きを最大 N 個まで保持するスロット群
template <class TParams, int N = 4>
struct PostEffectOverrides
{
    struct Slot
    {
        uint32_t source   = 0;    // 発生源の識別子。0 は空きスロット
        int      priority = 0;    // 大きいほど後に重なる（手前に出る）
        float    weight   = 0.0f; // 普段の設定から params へ寄せる割合（0〜1）。0 で無効
        TParams  params{};        // 寄せる先の目標値
    };

    static constexpr int CAPACITY = N;

    Slot slots[N];

    // 発生源 source のスロットを返す（まだ確保していなければ nullptr）
    const Slot *Find(uint32_t source) const
    {
        if (source == 0)
            return nullptr;

        for (const Slot &s : slots)
            if (s.source == source)
                return &s;

        return nullptr;
    }

    // 自分のスロットを返し、なければ空きを1つ確保する。満杯なら nullptr（その演出はあきらめる）
    // 同じ source なら同じスロットが返るので、毎フレーム呼んでよい
    Slot *Acquire(uint32_t source, int priority)
    {
        if (source == 0)
            return nullptr;

        Slot *freeSlot = nullptr;
        for (Slot &s : slots)
        {
            if (s.source == source)
            {
                s.priority = priority; // 優先度の変更を毎フレーム反映できるようにする
                return &s;
            }
            if (s.source == 0 && freeSlot == nullptr)
                freeSlot = &s;
        }

        if (freeSlot == nullptr)
            return nullptr;

        *freeSlot          = Slot{};
        freeSlot->source   = source;
        freeSlot->priority = priority;
        return freeSlot;
    }

    // 自分のスロットを手放す。演出が終わったときに呼ぶ（なければ何もしない）
    void Release(uint32_t source)
    {
        for (Slot &s : slots)
            if (s.source == source)
            {
                s = Slot{};
                return;
            }
    }

    // 効いている上書きが1つでもあるか（普段の設定がオフでも描くかの判定に使う）
    bool AnyActive() const
    {
        for (const Slot &s : slots)
            if (s.source != 0 && s.weight > 0.0f)
                return true;
        return false;
    }

    // 効いているスロットを priority の小さい順に fn(params, weight) へ渡す
    // 重ねて合成すると順番で結果が変わるので、スロットの並びではなく priority で決める
    // N が小さい前提の挿入ソートで、ヒープは使わない
    template <class TFunc>
    void ForEachActive(TFunc fn) const
    {
        int order[N];
        int count = 0;

        for (int i = 0; i < N; ++i)
            if (slots[i].source != 0 && slots[i].weight > 0.0f)
                order[count++] = i;

        for (int i = 1; i < count; ++i)
        {
            const int key = order[i];
            int       j   = i - 1;
            while (j >= 0 && slots[order[j]].priority > slots[key].priority)
            {
                order[j + 1] = order[j];
                --j;
            }
            order[j + 1] = key;
        }

        for (int i = 0; i < count; ++i)
        {
            const Slot &s = slots[order[i]];
            fn(s.params, (s.weight < 1.0f) ? s.weight : 1.0f);
        }
    }
};
