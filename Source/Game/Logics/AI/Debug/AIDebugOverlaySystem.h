#pragma once
// ===========================================================================
// 概要: ボスの AI の状態を、頭上とゲーム画面の左上にテキストで表示するデバッグ用の System（エディタのみ）。
//
// 責務:
//   - ボスの頭上の位置を画面座標に変換し、ImGui の前景描画リストへテキストを描く
//   - 1行目 [Combat]              … BossPhaseSystem が決めた形態
//     2行目 ボス：移動             … BT が今 Running にしている行動
//     3行目 12.3 m                … プレイヤーとの距離（BT の距離条件が比べている値そのもの）。
//                                   今いる帯（AIDebugSpatialSystem が地面に塗る帯）と同じ色で塗る
//     4行目 HP 895/1000 Drone 100 … ボスの HP とドローンの残数
//   - 見る場所を1か所にするため、距離も頭上の箱にまとめる
//   - ゲーム画面の左上に「技選択」のパネルを出す。最後に技を選んだときの上位3つの点数・確率・内訳と、
//     それまでに選んだ技の履歴（確率・距離）を表で並べる。まだ一度も選んでいなければ何も出さない
//   - OnGui で表示の ON/OFF・文字の大きさ・高さを変えられるようにする
//
// 備考: BossPhaseComponent と BehaviorTreeComponent の両方を読むので、どちらの System にも
//       入れず独立させている。
// ===========================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Debug/AIDebugRanges.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class AIDebugOverlaySystem : public CCL::ECS::IfSystem<
                                 AIDebugOverlaySystem,
                                 CCL::ECS::Read<BossPhaseComponent>,
                                 CCL::ECS::Read<BehaviorTreeComponent>,
                                 CCL::ECS::Read<TransformComponent>>
{
public:
    AIDebugOverlaySystem() : IfSystem("AIDebugOverlaySystem") {}

    /**
     * @brief AI の状態を画面上に描く。
     *
     * ImGui 前景リストへ直接描くので、System Manager のヘッダ開閉に関係なく、チェックが入っている間ずっと表示される。
     */
    void Update(float dt) override;

    // System Manager 内に表示する操作 UI。別ウィンドウは作らない。
    void OnGui() override;

private:
    // パネルの表のセル1つ
    struct PanelCell
    {
        std::string text;  // 中身
        uint32_t    color; // 文字の色（ImU32）
    };

    // パネルの表の行1つ
    struct PanelRow
    {
        std::vector<PanelCell> cells;             // 列の順に並べる
        bool                   highlight = false; // 背景を黄色くする（選ばれた技の行）
    };

    // パネルの表1つ。数字の意味が分かるよう、列には必ず見出しを付ける
    struct PanelTable
    {
        std::string              title;      // 表の上の見出し
        std::vector<std::string> headers;    // 列の見出し。空なら見出しだけの箱になる
        std::vector<bool>        rightAlign; // 列ごとに右揃えにするか（数字の列）
        std::vector<PanelRow>    rows;       // 行
    };

    // 技選択のパネル（最後の選択の表と、それまでの選択の表）を (left, top) から描き、使った高さを返す（px）
    float DrawScorePanel(const BehaviorTreeComponent &bt, float left, float top);

    // 表を1つ (left, top) から描き、使った高さを返す（px）。背景は描かない。
    // inOutWidth は今までに描いた表の幅の最大で、この表の方が広ければ書き換える
    float DrawPanelTable(const PanelTable &table, float left, float top, float &inOutWidth);

    // 技選択の子の表示名。エディタの技選択の一覧と同じ決め方（Cooldown などの修飾を下へたどり、
    // 名前の付いたまとまりを探す）。名前が無ければ枝の中の最初の行動の名前
    static const char *TechniqueName(const BTAsset &asset, BTNodeID nodeId);

    // 枝の中で最初に出てくる行動の名前。無ければ nullptr
    static const char *FirstActionName(const BTAsset &asset, BTNodeID nodeId);

    // 子の優先度。採点ルールがあればその base、無ければ weight（BTUtilityScore の ScoreChild と同じ）
    static float BasePriority(const BTAsset &asset, BTNodeID nodeId);

    //bool  m_enabled     = true;
    bool  m_enabled     = false;
    float m_fontSize    = 25.0f; // 文字の大きさ（ピクセル）
    float m_headOffsetY = 15.0f; // キャラの原点から頭上までのワールド Y オフセット（m）。体に重ならない高さにする

    // プレイヤーとの距離 1m あたりに足す頭上の高さ（m）。文字の大きさは画面上で一定なのに、
    // 遠いとボスは小さく映り、同じ高さだと文字の枠がボスに被る。離れるほど持ち上げてかわす。
    float m_headOffsetPerMeter = 0.12f;

    // 頭上の箱に、ボスとプレイヤーの距離の行を出すか。
    bool m_showDistance = true;

    // ゲーム画面の左上に技選択のパネルを出すか
    bool  m_showScores    = true;
    float m_panelFontSize = 18.0f; // パネルの文字の大きさ（ピクセル）
    int   m_historyLines  = 5;     // パネルに出す「それまで」の選択の数。最大 kPickLogSize - 1

    // 距離の数字の色を決めるための間合いのリング。毎フレーム木から拾い直す（配列の確保は使い回す）。
    std::vector<AIDebugRanges::RangeRing> m_rings;
    std::vector<float>                    m_edgeRings; // CollectRings が一緒に集める。ここでは使わない
};
