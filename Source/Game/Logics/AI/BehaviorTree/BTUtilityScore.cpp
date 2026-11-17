#include "BTUtilityScore.h"

#include <algorithm>

namespace CCL::AI::BT
{
    float ScoreChild(const BTAsset &asset, BTNodeID childId, float distanceToTarget, float *outDistance)
    {
        const BTNode &child = asset.nodes[childId];

        // ルールのない子は weight をそのまま点数にする。weight だけで組んだ木（Step 1 の形）がそのまま動き、
        // ルールを付けた子だけ点数が距離で変わる
        // 添字が範囲外になるのは読み込みの不具合だけだが、並列の評価で範囲外を読むと原因の分かりにくい落ち方をするので見ておく
        if (child.scoreRuleIndex < 0 || child.scoreRuleIndex >= static_cast<int>(asset.scoreRules.size()))
        {
            if (outDistance)
            {
                *outDistance = -1.0f;
            }
            return child.weight;
        }

        const BTScoreRule &rule     = asset.scoreRules[child.scoreRuleIndex];
        const float        distance = DistanceFactor(rule, distanceToTarget);
        if (outDistance)
        {
            *outDistance = distance;
        }
        return rule.base * distance;
    }



    float DistanceFactor(const BTScoreRule &rule, float distance)
    {
        // 台形の形（距離が左から右へ増える）:
        //   0 のまま ─ 0→1 へ上がる ─ bestMin ─ 1 のまま ─ bestMax ─ 1→0 へ下がる ─ 0 のまま
        //              （falloff の幅）                              （falloff の幅）
        //
        // 範囲の外では「はみ出した距離 ÷ ぼかし幅」だけ 1 から減らす
        // 例：範囲 3〜8m、ぼかし 2m で距離 9m なら、はみ出しは 1m なので 1 − 1/2 = 0.5

        // 得意な範囲の中なら満点
        if (distance >= rule.bestMin && distance <= rule.bestMax)
        {
            return 1.0f;
        }

        // ぼかしがなければ、範囲の外はすぐ 0 点（0 で割らないよう、ここで先に返す）
        if (rule.falloff <= 0.0f)
        {
            return 0.0f;
        }

        // 範囲からどれだけはみ出したか。近すぎる側と遠すぎる側のどちらか一方だけが正になる
        const float outside = (distance < rule.bestMin) ? (rule.bestMin - distance) : (distance - rule.bestMax);

        // ぼかし幅以上はみ出したら 0 点（マイナスにはしない）
        return (std::max)(0.0f, 1.0f - outside / rule.falloff);
    }



    float RepeatFactor(float remainingSeconds)
    {
        // 選んだ直後の倍率。0 %にしないのは、候補が1つしかないときに「何も出さない」にしないため
        // 0.3 なら、同じ点の技が並んでいても直前の技は 3 割の重みでしか引かれない
        constexpr float kFloor = 0.3f;

        // 経過の割合（0＝選んだ直後、1＝減点が消えた）。0〜1 の外にはみ出さないよう抑える
        const float elapsed = 1.0f - std::clamp(remainingSeconds / kRepeatPenaltySeconds, 0.0f, 1.0f);

        // 選んだ直後は kFloor、時間が経つほど直線で 1 へ戻る
        return kFloor + (1.0f - kFloor) * elapsed;
    }
}
