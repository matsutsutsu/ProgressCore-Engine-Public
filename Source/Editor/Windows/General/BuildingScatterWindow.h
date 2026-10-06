#pragma once
// ===========================================================================
// 概要: 中心の円柱ステージの周りに、建物モデル（Scifi-Building_01〜08）を円環状に
//       ランダム配置するウィンドウ。
//
// 責務:
//   - 距離・数・間隔・スケール・回転・使うモデルなどの設定
//   - 建物群の生成と、このウィンドウが生成したものの一括削除
//
// 備考: 生成した建物は親グループ（空のエンティティ）の下にまとめ、ヒエラルキーを
//       平らにしない。名前は通し番号で必ず一意にする。既定は非表示。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include <vector>

class BuildingScatterWindow : public EditorWindow
{
public:
    // 既定は非表示（Window メニューから開く）
    BuildingScatterWindow() : EditorWindow("Building Scatter", false) {}

protected:
    void DrawContents(EditorContext &context) override;

private:
    // 建物群を1回ぶん生成し、クラスタごとの親グループの下にまとめる
    void Generate(EditorContext &context);

    // このウィンドウが生成した全グループ（と配下の建物）を削除する
    void DeleteAll(EditorContext &context);

    // ---- 配置パラメータ ----
    float m_center[3]     = {0.0f, 0.0f, 0.0f}; // 中心（円柱ステージ）のワールド座標
    float m_minRadius     = 500.0f;             // 中心からの最短距離 [m]（これより内側には置かない）
    float m_maxRadius     = 1000.0f;            // 中心からの最長距離 [m]
    int   m_count         = 60;                 // 生成する建物の数
    float m_baseY         = -300.0f;            // 建物の接地の高さ（親グループからの Y）[m]
    int   m_seed          = 1234;               // 乱数のシード（変えると配置が変わる）
    bool  m_randomYaw     = true;               // Y 軸まわりにランダムに回すか
    float m_scaleRange[2] = {5.0f, 13.0f};      // 均等スケールの下限・上限
    float m_minSpacing    = 300.0f;             // 建物同士の最小の間隔 [m]（重なり防止）
    int   m_clusterCount  = 6;                  // 親グループ（塊）の数。1 なら全部を1つにまとめる

    // 使う建物モデル（Scifi-Building_01〜08）。既定は全部
    bool m_useModel[8] = {true, true, true, true, true, true, true, true};

    // ---- 内部状態 ----
    int m_batchCounter = 0; // 生成した回数（グループ名を一意にするため）
    int m_nameCounter  = 0; // 建物の通し番号（名前を一意にするため。戻さない）

    std::vector<CCL::ECS::EntityID> m_spawnedGroups;        // 生成した親グループ（全削除で使う）
    int                             m_lastSpawnedCount = 0; // 直近に生成した建物の数（表示用）
};
