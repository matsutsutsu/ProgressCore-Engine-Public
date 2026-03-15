#pragma once

// 大きな静的メッシュ（地形、建物など）をマークするコンポーネント
struct StaticMeshComponent {
	bool isLargeMesh = true;  // グリッド外からも常に判定対象にする
};