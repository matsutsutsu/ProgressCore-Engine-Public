#pragma once
// ===========================================================================
// PostEffectOverrides.h
// ---------------------
// 役割:
//   ポストプロセス設定への「一時的な上書き」を、発生源を問わず受け取る固定長スロット。
//
// なぜ必要か:
//   演出システムが設定コンポーネントを直接書き換えると、複数の演出が重なったときに
//   後勝ちで消し合う。さらに終了時に「開始前の値」へ復元する実装だと、後から始まった
//   側が相手の変更済みの値を素の設定として保存し、シーンの設定ごと壊してしまう。
//   各発生源は自分のスロットだけを持ち、合成は PostProcessSystem が1箇所で行う。
//
// なぜ発生源を「名前付きフィールド」ではなく「キー」にするか:
//   burstJustEvade のような名前をフィールドにすると、演出を1つ増やすたびに
//   エンジン側のヘッダを編集することになり、描画エンジンがゲーム固有の演出名を
//   知ってしまう。キーなら名前も優先度も呼ぶ側が決められ、エンジンは中身を
//   知らないままでいられる。
//
// 使い方:
//   static const uint32_t kSource = CCL::Utils::HashString("MyEffect");
//   if (auto *ov = comp->overrides.Acquire(kSource, 10)) { ov->weight = env; ... }
//   ...演出終了時...
//   comp->overrides.Release(kSource);
// ===========================================================================
#include <cstdint>

// TParams 型の上書きを最大 N 個まで保持するスロット群。
template <class TParams, int N = 4>
struct PostEffectOverrides
{
    struct Slot
    {
        uint32_t source   = 0;    // 発生源の識別子。0 は空きスロット。
        int      priority = 0;    // 大きいほど後に重なる（＝手前に出る）。
        float    weight   = 0.0f; // 0 で無効。素の設定から params へ寄せる割合。
        TParams  params{};        // 寄せ先の目標値。
    };

    static constexpr int CAPACITY = N;

    Slot slots[N];

    /**
     * @brief 自分のスロットを返す。まだ無ければ空きを1つ確保する。
     * @return 満杯で確保できなければ nullptr。呼び出し側はその演出を諦める。
     * @note   同じ source で2回呼んでも同じスロットが返る（毎フレーム呼んでよい）。
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

    // 自分のスロットを手放す。演出終了時に呼ぶ。
    void Release(uint32_t source)
    {
        for (Slot &s : slots)
            if (s.source == source)
            {
                s = Slot{};
                return;
            }
    }

    // 効いている上書きが1つでもあるか（素の設定が OFF でも描画するかの判定用）。
    bool AnyActive() const
    {
        for (const Slot &s : slots)
            if (s.source != 0 && s.weight > 0.0f)
                return true;
        return false;
    }

    // 効いているスロットを priority 昇順に fn(params, weight) へ渡す。
    //
    // N が小さい前提の挿入ソート。ヒープ確保はしない。
    // 重ね合成では順序が結果を変えるため、宣言順ではなく priority で決める。
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
