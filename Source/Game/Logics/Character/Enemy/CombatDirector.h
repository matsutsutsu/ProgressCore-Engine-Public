#pragma once

// 攻撃の交通整理を行う監督クラス
struct CombatDirector {
    // 同時に攻撃できる最大数（この数を下げると優しくなり、上げると激しくなる）
    int maxAttackTokens = 3;

    // 現在使用中のトークン数
    int usedTokens = 0;

    // --- 攻撃許可を求める ---
    bool RequestToken()
    {
        if (usedTokens < maxAttackTokens) {
            usedTokens++;
            return true; // 許可
        }
        return false; // 却下（順番待ち）
    }

    // --- トークンを返却する ---
    void ReturnToken()
    {
        if (usedTokens > 0) {
            usedTokens--;
        }
    }

    // リセット用（シーン遷移時など）
    void Reset() { usedTokens = 0; }
};