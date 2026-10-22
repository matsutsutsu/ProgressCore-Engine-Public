#pragma once
// ===========================================================================
// 概要: ムービーの台本（ショットと4種類のトラック）を ImSequencer の帯として見せるアダプタ。
//
// 責務:
//   - 「秒」と「int のコマ」の往復
//   - 台本の項目を、1項目 = 1行に並べる（ショット → SE → エフェクト → シグナル → アニメ）
//   - 行の名前と帯の色
//   - 帯をドラッグした結果を、台本の時刻と長さへ書き戻す
//
// 備考:
//   台本の意味は扱わない（読み書きは CinematicScriptIO、絵を作るのは CinematicCameraSystem）。
//   ImSequencer は帯の両端を int のコマで受け取り、ドラッグ中はそのアドレスへ直接書き込む。
//   台本の時刻は float の秒なので、int の置き場所（m_frames）を別に持って往復させる。
//     台本 → m_frames : Begin()。掴んでいる間は写し直さない（ドラッグ中の値を毎フレーム巻き戻してしまう）
//     m_frames → 台本 : Flush()。掴んだ帯が実際に動いたときだけ書き戻す（クリックしただけで
//                       書き戻すと、欄に打った 4.41 が刻みの 4.4333 へ丸められる）
//   点（SE・エフェクト・シグナル）は長さが無いので、1コマ幅の帯で描き、端を掴んでも長さは変えない。
// ===========================================================================
#include "ImSequencer.h"

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

    // 1秒を何コマに分けるか。ゲームの1コマ（60fps）までは要らないが、0.1秒刻みでは帯の端を合わせにくい。
    static constexpr int kFramesPerSecond = 30;

    // 秒 → コマ。
    static int TimeToFrame(float time);

    // コマ → 秒。
    static float FrameToTime(int frame);

    /**
     * @brief 台本の項目が何行目に描かれるかを返す。
     * @param script 台本。
     * @param kind   項目の種類。
     * @param index  その種類の配列の添字。
     * @return 行の番号（ショット → SE → エフェクト → シグナル → アニメ の順に数える）。
     */
    static int RowOf(const CinematicScript &script, RowKind kind, std::size_t index);

    // 描く前に毎フレーム呼ぶ。見せる台本と、再生位置のコマを渡す。
    void Begin(CinematicScript &script, int cursorFrame);

    // 掴んでいた帯が動いていたら台本へ書き戻す。書き戻したら true。
    bool Flush();

    // row 行目が台本のどの項目か。行が無ければ false。
    bool RowAt(int row, Row &out) const;

    // ---- ここから下は ImSequencer が呼ぶ ----

    int GetFrameMin() const override { return 0; }
    int GetFrameMax() const override;
    int GetItemCount() const override { return static_cast<int>(m_rows.size()); }

    void BeginEdit(int index) override { m_editIndex = index; }
    void EndEdit() override;

    const char *GetItemLabel(int index) const override;

    /**
     * @brief index 行目の帯を ImSequencer に渡す。
     * @param index 行の番号。
     * @param start 帯の開始のコマへのポインタを受け取る。ImSequencer がドラッグで書き換える。nullptr 可。
     * @param end   帯の終了のコマへのポインタを受け取る。nullptr 可。
     * @param type  行の種類（RowKind の整数値）を受け取る。nullptr 可。
     * @param color 帯の色を受け取る。nullptr 可。
     */
    void Get(int index, int **start, int **end, int *type, unsigned int *color) override;

private:
    // 台本の項目を行へ並べ直し、帯の両端を m_frames へ写す。
    void Sync();

    // 行の項目 → 帯の両端のコマ。
    std::pair<int, int> ToFrames(const Row &row) const;

    CinematicScript *m_script = nullptr;
    int              m_cursor = 0; // 再生位置のコマ。帯の明暗に使う

    std::vector<Row>                 m_rows;
    std::vector<std::pair<int, int>> m_frames;

    int m_editIndex     = -1; // 掴んでいる帯。離すと -1
    int m_releasedIndex = -1; // 離したフレームだけ、離した帯の番号が入る

    mutable char m_label[128] = {}; // GetItemLabel の戻り値の置き場所（呼び出し側はすぐ描いて捨てる）
};
