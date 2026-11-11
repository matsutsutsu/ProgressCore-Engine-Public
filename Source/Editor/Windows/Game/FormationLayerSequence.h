#pragma once
// ===========================================================================
// 概要: 動きの層（FormationLayer）を ImSequencer の帯として見せるアダプタ
//
// 責務:
//   - 「0〜1 の時間帯」と「int のフレーム」の往復
//   - 帯を置ける区間の制限（Shiver は予告、それ以外は攻撃）
//   - 帯の上の絵（席ごとの動き出しの斜線、置けない区間の目隠し）
//
// 備考: 形の意味は扱わない（計算は DataDrivenFormation、画面の組み立ては FormationEditorWindow）
//       ImSequencer は帯の両端を int のフレームで受け、ドラッグ中はその場所へ直接書くので、m_frames を別に持って往復させる
//         Begin() でデータから写す。掴んでいる間は写し直さない（ドラッグ中の値を巻き戻してしまう）
//         Flush() で書き戻す。動いたときだけ書く（クリックだけで書くと、打った 0.333 が 0.34 に丸められる）
//       左の区間は予告（Shiver だけ）、右は攻撃。幅を秒数に比例させると、0.4秒ほどの攻撃の帯が潰れる
// ===========================================================================
#include "ImSequencer.h"

#include <utility>
#include <vector>

namespace CCL::AI::Formation
{
    struct FormationAsset;
    struct FormationLayer;
} // namespace CCL::AI::Formation

class FormationLayerSequence : public ImSequencer::SequenceInterface
{
public:
    using Asset = CCL::AI::Formation::FormationAsset;
    using Layer = CCL::AI::Formation::FormationLayer;

    // 各区間を何等分するか。攻撃が0.4秒なら1刻み8ミリ秒で、ゲームの1コマ(16ミリ秒)より細かい
    // ImSequencer は1刻みを既定で10pxで描くので、増やすと横スクロールが要るようになる
    static constexpr int kTelegraphFrames = 20;
    static constexpr int kAttackFrames    = 50;

    // 予告の始まりからの秒数 → タイムラインのフレーム
    static int TimeToFrame(const Asset &asset, float time);

    // タイムラインのフレーム → 予告の始まりからの秒数
    static float FrameToTime(const Asset &asset, int frame);

    // 描く前に毎フレーム呼ぶ。編集する陣形と、再生位置のフレームを渡す
    void Begin(Asset &asset, int cursorFrame);

    // 掴んでいた帯が動いていたらデータへ書き戻す。書き戻したら true
    bool Flush();

    // ---- ここから下は ImSequencer が呼ぶ ----

    int GetFrameMin() const override { return 0; }
    int GetFrameMax() const override { return kTelegraphFrames + kAttackFrames; }
    int GetItemCount() const override;

    void BeginEdit(int index) override { m_editIndex = index; }
    void EndEdit() override;

    const char *GetItemLabel(int index) const override;
    /**
     * @brief index 番目の層の帯を ImSequencer に渡す
     * @param index 層の番号
     * @param start 帯の開始フレームへのポインタを受け取る。ImSequencer がドラッグで書き換える。nullptr 可
     * @param end   帯の終了フレームへのポインタを受け取る。nullptr 可
     * @param type  層の種類（LayerType の整数値）を受け取る。nullptr 可
     * @param color 帯の色を受け取る。nullptr 可
     */
    void        Get(int index, int **start, int **end, int *type, unsigned int *color) override;

    /**
     * @brief 帯の上に斜線を、行の上に予告と攻撃の境目と、その帯を置けない側の区間の目隠しを描く
     *
     * 斜線1本 = 1つの席（列・段）が動き始めてから動き切るまで。扇状に広がっていれば「波」
     */
    void CustomDrawCompact(int index, ImDrawList *draw, const ImRect &rc, const ImRect &clip) override;

private:
    // 帯を置ける区間。Shiver は予告、それ以外は攻撃
    struct Section
    {
        int first;
        int length;
    };
    static Section SectionOf(const Layer &layer); // 層を置ける区間（Shiver なら予告、それ以外は攻撃）

    // 層の時間帯 → 帯の両端のフレーム
    static std::pair<int, int> ToFrames(const Layer &layer);

    // 全層の時間帯を m_frames へ写す
    void Sync();

    Asset *m_asset  = nullptr;
    int    m_cursor = 0; // 再生位置のフレーム。帯の明暗に使う

    std::vector<std::pair<int, int>> m_frames;

    int m_editIndex     = -1; // 掴んでいる帯。離すと -1
    int m_releasedIndex = -1; // 離したフレームだけ、離した帯の番号が入る

    mutable char m_label[64] = {}; // GetItemLabel の戻り値の置き場所（呼び出し側はすぐ描いて捨てる）
};
