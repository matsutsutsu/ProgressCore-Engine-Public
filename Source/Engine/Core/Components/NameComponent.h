#pragma once
// ===========================================================================
// 概要: Entity の表示名を持つコンポーネント NameComponent。
//
// 責務:
//   - エディタのヒエラルキー表示・ログ（EntityLabel）で使う名前を保持する
// ===========================================================================
#include <cstring> // strncpy_s

struct NameComponent
{
    // 固定長にしてチャンク内へ直に置く。std::string だとヒープ参照が挟まり、
    // 全エンティティ走査時のキャッシュ効率が落ちるため。32文字で足りている。
    char name[32] = "Entity";

    NameComponent() = default;
    NameComponent(const char *str)
    {
        if (str)
        {
            // 32文字を超える名前は切り詰める（バッファオーバーラン防止）
            strncpy_s(name, str, sizeof(name) - 1);
        }
    }
};