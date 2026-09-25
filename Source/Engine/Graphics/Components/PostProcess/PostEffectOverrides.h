#pragma once
// ===========================================================================
// 概要: ポストプロセス設定への一時的な上書きを、発生源ごとに受け取る固定長スロット。
//
// 責務:
//   - 発生源（キー）ごとにスロットを確保・解放する
//   - 効いているスロットを priority の小さい順に列挙する
//
// 備考:
//   なぜ必要か: 演出システムが設定コンポーネントを直接書き換えると、複数の演出が
//   重なったときに後から書いた方が勝ち、互いに消し合う。終了時に「開始前の値」へ
//   戻す作りだと、後から始まった演出が、先の演出が変えた値を普段の設定として覚えてしまい、
//   シーンの設定ごと壊れる。各発生源は自分のスロットだけを持ち、合成は
//   PostProcessSystem が1か所で行う。
//
//   なぜ名前付きフィールドではなくキーにするか: burstJustEvade のような名前を
//   フィールドにすると、演出を1つ増やすたびにエンジンのヘッダを直すことになり、
//   描画エンジンがゲーム固有の演出名を知ってしまう。キーなら名前も優先度も呼ぶ側が決め、
//   エンジンは中身を知らずに済む。
//
// @code
//   static const uint32_t kSource = CCL::Utils::HashString("MyEffect");
//   if (auto *ov = comp->overrides.Acquire(kSource, 10)) { ov->weight = env; ... }
//   // 演出が終わったら
//   comp->overrides.Release(kSource);
// @endcode
// ===========================================================================
#include <cstdint>

// TParams 型の上書きを最大 N 個まで保持するスロット群。
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

    /**
     * @brief 自分のスロットを返す。まだ無ければ空きを1つ確保する。
     * @return 満杯で確保できなければ nullptr。呼び出し側はその演出をあきらめる。
     * @note 同じ source で何度呼んでも同じスロットが返るので、毎フレーム呼んでよい。
     */
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

    // 自分のスロットを手放す。演出が終わったときに呼ぶ
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

    // 効いているスロットを priority の小さい順に fn(params, weight) へ渡す。
    // 重ねて合成すると順番で結果が変わるので、スロットの並びではなく priority で決める。
    // N が小さい前提の挿入ソートで、ヒープは使わない。
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
