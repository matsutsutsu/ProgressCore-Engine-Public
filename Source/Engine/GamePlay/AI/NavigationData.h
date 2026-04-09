#pragma once

class dtNavMesh; // 前方宣言

// Worldに1つだけ存在するナビゲーション用の共有データ
struct NavigationData {
    dtNavMesh* navMesh = nullptr;
    bool showDebugDraw = true; // デバッグ描画のON/OFF用

    // デストラクタでメモリリークを防ぐ
    ~NavigationData() {
        if (navMesh) {
            // dtFreeNavMesh(navMesh); // DetourAlloc.hが必要になるため、後で安全な破棄を実装します
        }
    }
};