#pragma once
#include <cstring> // for strncpy_s

struct NameComponent {
    // 固定長にしてチャンク内へ直に置く。std::string だとヒープ参照が挟まり、
    // 全エンティティ走査時のキャッシュ効率が落ちるため。32文字で足りている。
    char name[32] = "Entity";

    // コンストラクタで初期化しやすくする
    NameComponent() = default;
    NameComponent(const char *str)
    {
        if (str) {
            // 安全にコピー（バッファオーバーラン防止）
            strncpy_s(name, str, sizeof(name) - 1);
        }
    }
};