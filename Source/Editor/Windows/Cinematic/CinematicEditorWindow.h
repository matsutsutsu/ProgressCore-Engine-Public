#pragma once
// ===========================================================================
// 概要: ムービーの台本（Assets/Cinematics/*.json）を時間軸で見て編集するウィンドウ。
//
// 責務:
//   - 台本を新しく作る・開く・保存する。保存したらゲーム側に読み直させる
//   - 保存した台本を、頭から・タイムラインの再生位置から、ゲームと同じ再生要求で流させる
//   - ショットと4種類のトラックを ImSequencer の帯で並べ、ドラッグで時刻と長さを変える
//   - 項目を足す・消す、選んだ項目の数値を直す
//   - 一時停止中、再生位置の絵をゲーム画面に出す（プレビュー。カメラだけで、トラックは出さない）
//   - 今の画面のカメラ位置を、ショットの開始・終了へ取り込む
//
// 備考:
//   台本の意味は扱わない（読み書きは CinematicScriptIO、帯の並べ方は CinematicTimelineSequence、
//   絵を作るのは CinematicCameraSystem）。
//   編集はウィンドウが持つ写し（m_script）に対して行う。再生中のムービーが持つ台本は触らない。
//   Undo は持たない。保存する前なら「開き直す」で元に戻せる。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Cinematic/CinematicScript.h"

#include <memory>
#include <string>

class CinematicTimelineSequence;

class CinematicEditorWindow : public EditorWindow
{
public:
    // 既定は非表示。Window メニューから開く。
    CinematicEditorWindow();

    // CinematicTimelineSequence の定義はこのヘッダに無いので、破棄も .cpp で行う。
    ~CinematicEditorWindow() override;

protected:
    void DrawContents(EditorContext &context) override;

    /**
     * @brief 閉じられた・畳まれたフレームでも必ず呼ばれる。プレビューを出す・止めるのに使う。
     *
     * プレビュー中はムービー用カメラが画面を掌握するので、ウィンドウを閉じた瞬間に止めないと、
     * ゲーム画面がプレビューの絵のまま戻らなくなる。
     */
    void OnFrameBegin(EditorContext &context) override;

private:
    // ---- 画面の部品 ----

    // 新規・開く・開き直す・保存・名前を付けて保存・ゲームで再生・ここから再生のボタンと、開いている台本の名前。
    void DrawFileBar(EditorContext &context);

    // 全体の長さ・開始待ち・スキップ可否。値を変えたら true。
    bool DrawScriptSettings();

    // プレビューの切り替えと、再生位置の説明。
    void DrawPreviewBar(EditorContext &context);

    // ショットとトラックの帯と、再生位置。帯を動かしたら true。
    bool DrawTimeline();

    // 項目を足す・消すボタン。台本を変えたら true。
    bool DrawItemButtons();

    // 選んでいる項目の数値。値を変えたら true。
    bool DrawInspector(EditorContext &context);

    // ---- 種類ごとの数値の欄。値を変えたら true ----
    bool DrawShotFields(EditorContext &context, CameraShot &shot);
    bool DrawSoundFields(EditorContext &context, CinematicSound &sound);
    bool DrawEffectFields(EditorContext &context, CinematicEffect &effect);
    bool DrawSignalFields(CinematicSignal &signal);
    bool DrawAnimationFields(EditorContext &context, CinematicAnimation &anim);

    /**
     * @brief 基準にする Entity の名前の欄。シーンに居なければ、その場で知らせる。
     * @param context    エディタの文脈。
     * @param subject    名前。
     * @param emptyMeans 空のときの意味（例: "ワールドの原点"）。nullptr なら空は許さない。
     * @return 値を変えたら true。
     */
    bool DrawSubjectField(EditorContext &context, std::string &subject, const char *emptyMeans);

    // ---- 操作 ----

    /**
     * @brief 今の画面のカメラ位置・向き・画角を、ショットの開始か終了へ取り込む。
     *
     * 基準の今の位置と向きから見たずれに直して書く。Truck はカメラ位置、Orbit は角度・半径・高さ。
     * 注視点は、視線の上で基準に一番近い点にする。
     *
     * @param context エディタの文脈。
     * @param shot    書き込む先のショット。
     * @param toEnd   true なら終了の値、false なら開始の値へ書く。
     * @return 取り込めたら true。基準が見つからない・画面のカメラが無いときは false（理由は m_status）。
     */
    bool CaptureView(EditorContext &context, CameraShot &shot, bool toEnd);

    /**
     * @brief ムービー用カメラへ、見せる台本と時刻を渡す（または止める）。
     * @param context エディタの文脈。
     * @param active  見せるなら true。
     */
    void PushPreview(EditorContext &context, bool active);

    /**
     * @brief 台本を読んで、編集する写しにする。
     * @param path      読む台本のパス。
     * @param keepView  再生位置と選んでいる行を残すか（保存の直後に読み直すとき）。
     * @note 読めなかったときは、今開いている台本をそのまま残す（理由は m_status とログに出る）。
     */
    void Open(const std::string &path, bool keepView);

    // 何も無い台本にする。開いていた台本の、保存していない変更は捨てる。
    void NewScript();

    /**
     * @brief 今のパスへ保存する。保存できたら読み直し、ゲーム側にも読み直させる。
     * @return 保存できたら true。書けない・決まりに合わないときは false（理由は m_status）。
     * @note まだ一度も保存していない（パスが無い）台本は、保存先を選ばせる（SaveAs）。
     */
    bool Save(EditorContext &context);

    // 保存先を選んで保存する。新しい台本の初めての保存と、別名で残すときに使う。
    void SaveAs(EditorContext &context);

    // ---- 状態 ----

    CinematicScript m_script;        // 編集する写し。path が空なら、まだ一度も保存していない新しい台本
    bool            m_dirty = false; // 保存していない変更があるか（値を触ったら立てる。元へ戻したかまでは見ない）
    std::string     m_status;         // 直近の読み込み・保存・取り込みの結果

    // プレビュー
    bool m_previewEnabled = true;  // 一時停止中に、再生位置の絵をゲーム画面へ出すか
    bool m_previewPushed  = false; // ムービー用カメラへプレビューを渡しているか（止めるのを1回で済ませる）

    // タイムライン
    std::unique_ptr<CinematicTimelineSequence> m_sequence;          // 台本と ImSequencer のつなぎ
    float                                      m_time        = 0.0f; // 再生位置（秒）
    int                                        m_selectedRow = -1;   // 選んでいる行。下に数値を出す
    int                                        m_firstFrame  = 0;    // 横スクロールの位置（ImSequencer が書く）
};
