#pragma once
#include "ECS/System/CCL_System.h"
#include "../Generation/MapGenerator.h" // Config構造体のため必要
#include <string>
#include <vector>

// 前方宣言
struct ID3D11Device;
class ShapeRenderer;

class MapDebugSystem : public CCL::ECS::SystemBase {
  private:
    // --- 定数・編集用データ ---
    static const int MAX_TEMPLATE_SIZE                              = 100;
    int              editRoom[MAX_TEMPLATE_SIZE][MAX_TEMPLATE_SIZE] = {0};

    // 現在編集中のサイズ
    int currentW = 15;
    int currentH = 15;

    // パレット (0:床, 1:壁, 2:穴)
    int selectedTile = 1;

    // テスト配置用のターゲット座標
    int targetGridX = 15;
    int targetGridZ = 15;

    // エクスポート用ファイル名
    char _exportFileName[64] = "Stage1_NewRoom";

    // --- ファイル読み込み用メンバ ---
    std::vector<std::string> _fileList;            // ファイル名のリスト
    int                      _selectedFileIdx = 0; // 現在選択中のインデックス

    // --- マップ生成コンフィグ ---
    MapGenerator::Config _genConfig;
    int                  _targetStageIndex  = 1; // プリセット読み込み用
    bool                 _requestRegenerate = false;

    ID3D11Device *_device = nullptr;

  public:
    MapDebugSystem();

    void SetDevice(ID3D11Device *device);

    // デバッグ描画 (3D)
    void OnDrawDebug(ShapeRenderer *renderer) override;

    // GUI描画 (ImGui)
    void OnGui() override;

    // 更新処理
    void Update(float dt) override;

  private:
    // GUIパーツ
    void DrawRoomEditor();
    void DrawGeneratorConfig();

    // アクション
    void SaveTemplateToJson();
    void ApplyToMap();
    void ExecuteRegeneration();

    // ★追加機能: 読み込み関連
    void ScanTemplateFiles();
    void LoadTemplateFromJson();
};