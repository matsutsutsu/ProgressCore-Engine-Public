#pragma once

// フィニッシュ演出が進んでいるか。
// FinishPresentationSystem が書き、リザルトの表示を待たせるのに読む
struct FinishPresentationState
{
    bool active = false; // とどめのスローを保っている間 true（ムービーへ移ったら false）
};
