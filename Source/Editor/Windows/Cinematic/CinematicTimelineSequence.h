#pragma once
// ===========================================================================
// 概要: ムービーの台本を ImSequencer の帯として見せるアダプタ
//
// 責務:
//   - 台本の項目を1項目1行に並べ（ショット → SE → エフェクト → シグナル → アニメ）、名前と色を付ける
//   - 帯をドラッグした結果を、台本の時刻と長さへ書き戻す。リップルなら後ろの項目もずらす
//
// 備考: ImSequencer は帯の両端を int のコマで受け、ドラッグ中はそこへ直接書く。台本は秒なので m_frames を挟む
//       掴んでいる間は台本から写し直さない（ドラッグ中の値を巻き戻してしまう）
//       書き戻すのは動いたときだけ（クリックだけで書くと、打った 4.41 が 4.4333 に丸められる）
// ===========================================================================
#include "ImSequencer.h"

#include "Editor/Windows/Cinematic/CinematicRipple.h"

#include <cstddef>
#include <utility>
#include <vector>

struct CinematicScript;

class CinematicTimelineSequence : public ImSequencer::SequenceInterface
{
public:
    // 行が台本のどの種類の項目か
    enum class RowKind
    {
        Shot,
        Sound,
        Effect,
        Signal,
        Animation,
    };

    // 行が指す台本の項目
    struct Row
    {
        RowKind     kind;
        std::size_t index; // その種類の配列の添字
    };

    // 1秒のコマ数。0.1秒刻みでは帯の端を合わせにくい
    static constexpr int kFramesPerSecond = 30;

    static int   TimeToFrame(float time);
    static float FrameToTime(int frame);

    // 台本の項目が何行目に描かれるか
    static int RowOf(const CinematicScript &script, RowKind kind, std::size_t index);

    // 描く前に毎フレーム呼ぶ。ripple なら、ショットの帯を動かしたときに後ろの項目もずらす
    void Begin(CinematicScript &script, int cursorFrame, bool ripple);

    // 掴んでいた帯が動いていたら台本へ書き戻す。書き戻したら true
    bool Flush();

    // row 行目が台本のどの項目か。行がなければ false
    bool RowAt(int row, Row &out) const;

    // ---- ここから下は ImSequencer が呼ぶ ----

    int GetFrameMin() const override { return 0; }
    int GetFrameMax() const override;
    int GetItemCount() const override { return static_cast<int>(m_rows.size()); }

    // 帯を掴んだ瞬間に1回だけ呼ばれる。ショットの帯なら、リップルの控えを取る
    void BeginEdit(int index) override;
    void EndEdit() override;

    const char *GetItemLabel(int index) const override;

    // 長さを持つショットとアニメだけ。点（SE・エフェクト・シグナル）はどこを掴んでも丸ごと動く
    bool CanResize(int index) const override;

    // index 行目の帯の両端のコマ（ImSequencer がドラッグで書き換える）・種類・色を渡す。どの出力も nullptr 可
    void Get(int index, int **start, int **end, int *type, unsigned int *color) override;

private:
    // 台本の項目を行へ並べ直し、帯の両端を m_frames へ写す
    void Sync();

    // 行の項目の、帯の両端のコマ
    std::pair<int, int> ToFrames(const Row &row) const;

    CinematicScript *m_script = nullptr;
    int              m_cursor = 0; // 再生位置のコマ。帯の明暗に使う

    std::vector<Row>                 m_rows;   // 行と台本の項目の対応
    std::vector<std::pair<int, int>> m_frames; // 行ごとの帯の両端のコマ

    int m_editIndex     = -1; // 掴んでいる帯。離すと -1
    int m_releasedIndex = -1; // 離したフレームだけ、離した帯の番号が入る

    bool                      m_rippleEnabled = true; // ショットの帯を動かしたとき、後ろの項目もずらすか
    CinematicRipple::Snapshot m_ripple;               // 掴んだ瞬間の、後ろの項目の時刻

    mutable char m_label[128] = {}; // GetItemLabel の戻り値の置き場所
};
