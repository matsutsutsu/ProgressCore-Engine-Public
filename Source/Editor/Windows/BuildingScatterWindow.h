/// @file  BuildingScatterWindow.h
/// @brief 中心の円柱ステージ周辺に建物モデル(Scifi-Building_01〜08)を
///        円環状にランダム配置するテスト用エディタウィンドウ。
///        役割     : パラメータを見ながら建物群を「生成」「削除」する。
///        責務     : 最短/最長距離・数・スケール・回転・クラスタ(親グループ)数などを
///                   指定し、中心から一定距離離した位置にランダム配置する。
///        位置付け : 既存 EditorWindow 群の一つ。Window メニューからトグルする。
///        設計方針 : 生成物は親グループ(空エンティティ)配下にまとめ、ヒエラルキーを
///                   フラットにしない。名前は連番で必ず一意にする。
#pragma once
#include "Editor/Core/EditorWindow.h"
#include <vector>

class BuildingScatterWindow : public EditorWindow
{
public:
    // 既定は非表示（必要時に Window メニューから開く）。
    BuildingScatterWindow() : EditorWindow("Building Scatter", false) {}

protected:
    void DrawContents(EditorContext& context) override;

private:
    // 建物群を1バッチ生成する（親グループ配下にまとめる）。
    void Generate(EditorContext& context);
    // このウィンドウが生成した全グループ(と配下の建物)を削除する。
    void DeleteAll(EditorContext& context);

    // ---- 配置パラメータ ----
    float _center[3]    = { 0.0f, 0.0f, 0.0f }; // 中心(円柱ステージ)のワールド座標
    float _minRadius    = 500.0f;                // 中心からの最短出現距離(これより内側には出さない)
    float _maxRadius    = 1000.0f;                // 最長出現距離
    int   _count        = 60;                   // 生成する建物数
    float _baseY        = -300.0f;                 // 建物の接地Y座標
    int   _seed         = 1234;                 // 乱数シード(変えると配置が変わる)
    bool  _randomYaw    = true;                 // Y軸ランダム回転
    float _scaleRange[2]= { 5.0f, 13.0f };       // ランダム均等スケールの下限/上限
    float _minSpacing   = 300.0f;                 // 建物同士の最小間隔(重なり防止)
    int   _clusterCount = 6;                    // 親グループ(塊)の数。1なら全部ひとつにまとめる

    // 使用する建物モデル(Scifi-Building_01〜08)。全ON既定。
    bool  _useModel[8]  = { true, true, true, true, true, true, true, true };

    // ---- 内部状態 ----
    int   _batchCounter = 0; // 生成バッチ番号(グループ名のユニーク化)
    int   _nameCounter  = 0; // 建物ごとの通し番号(名前のユニーク化。リセットしない)

    // 生成した親グループの EntityID(全削除で使用)。
    std::vector<CCL::ECS::EntityID> _spawnedGroups;
    int   _lastSpawnedCount = 0; // 直近生成した建物数(表示用)
};
