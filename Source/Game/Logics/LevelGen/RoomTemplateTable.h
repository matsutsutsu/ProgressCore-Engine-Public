#pragma once
//============================================================================
//  RoomTemplateTable.h
//  RuneForge ダンジョン生成 — 部屋テンプレート定義（仕様書 §6）
//
//  テンプレートはコード内テーブルとして関数で返す（後で外部データ化しやすいように）。
//  90°単位の回転バリエーションをサポートする。
//============================================================================
#include "DungeonTypes.h"
#include <vector>

namespace RuneForge::Dungeon
{
    //------------------------------------------------------------------------
    // 部屋テンプレート
    //  solidMask[x][z] = true で壁。x が width 方向、z が height 方向。
    //------------------------------------------------------------------------
    struct RoomTemplate
    {
        RoomType                       type = RoomType::Combat;
        int                            width = 0;   // セル単位の寸法（X方向）
        int                            height = 0;  // セル単位の寸法（Z方向）
        std::vector<std::vector<bool>> solidMask;   // solidMask[x][z]。true=壁
        std::vector<DoorInfo>          doors;       // ドア宣言（辺・セル位置）
        std::vector<SpawnMarker>       spawnMarkers;// 配置候補
        std::vector<int>               allowedRotations; // {0,90,180,270} のサブセット
    };

    //------------------------------------------------------------------------
    // テンプレートテーブルへのアクセス
    //------------------------------------------------------------------------

    // 全テンプレートを返す（コード内テーブル）
    const std::vector<RoomTemplate>& GetRoomTemplateTable();

    // 指定 RoomType に一致するテンプレートのインデックス一覧を返す
    std::vector<int> GetTemplatesForType(RoomType type);

    // テンプレートを 90°単位で回転して新しい RoomTemplate を返す。
    // rotationDeg は 0 / 90 / 180 / 270 のいずれか。
    RoomTemplate RotateTemplate(const RoomTemplate& src, int rotationDeg);

} // namespace RuneForge::Dungeon