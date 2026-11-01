#pragma once
// ===========================================================================
// 概要: ムービーの台本（Assets/Cinematics/*.json）を時間軸で見て編集するウィンドウ。
//
// 責務:
//   - 台本を新しく作る・開く・保存する。保存したらゲーム側に読み直させる
//   - 保存した台本を、頭から・タイムラインの再生位置から、ゲームと同じ再生要求で流させる
//   - ショットと4種類のトラックを ImSequencer の帯で並べ、ドラッグで時刻と長さを変える
//   - 項目を足す・消す、選んだ項目の数値を直す（何も選んでいなければ台本の設定）
//   - 再生位置の絵をゲーム画面に出す（プレビュー。カメラと被写界深度だけで、トラックは出さない）。
//     出し方は「出さない / 一時停止中だけ / 止めずに出す」。止めずに出す間は、ムービー中として操作と敵の思考が止まる
//   - 再生位置を実時間で進めてプレビューを流す（エディタ内の再生）
//   - ゲームでこの台本を流している間、再生位置をムービーに付いていかせる
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

#include <cstdint>
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
    // ゲーム画面への出し方（kPreviewModeNames と同じ並び）
    enum class PreviewMode : std::uint8_t
    {
        Off,         // 出さない
        WhilePaused, // 一時停止中だけ出す
        Live,        // 止めずに出す（操作と敵の思考は止まる）
    };

    // ---- 画面の部品 ----

    // 上の帯の1行目。新規・開く・開き直す・保存・名前を付けて保存と、開いている台本の名前
    void DrawToolbar(EditorContext &context);

    // 上の帯の2行目。再生・頭へ・ループ・再生位置と全体の長さ・画面への出し方・ゲームで流す
    void DrawTransportBar(EditorContext &context);

    // 上の帯の3行目。今の状態の注意か、直近の結果を1行だけ出す
    void DrawStatusLine(EditorContext &context);

    // 項目を足すボタン（再生位置に置く）。足したら true
    bool DrawAddButtons();

    // ショットとトラックの帯と、再生位置。帯を動かしたら true
    bool DrawTimeline();

    // 選んでいる項目の名前・消すボタン・数値。何も選んでいなければ台本の設定。値を変えたら true
    bool DrawInspector(EditorContext &context);

    // 開始待ち・スキップ可否。値を変えたら true
    bool DrawScriptSettings();

    // 選んでいる項目を台本から消し、選択を外す
    void EraseSelected();

    // 今、ゲーム画面にプレビューを出すか（ウィンドウが開いていて、出し方の条件に合うか）
    bool IsPreviewShown(const EditorContext &context) const;

    // ---- 種類ごとの数値の欄。値を変えたら true ----
    bool DrawShotFields(EditorContext &context, CameraShot &shot);
    bool DrawShotDofFields(EditorContext &context, CameraShot &shot);
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

    /**
     * @brief ショットの開始か終了での、ピントの距離（カメラから注視点まで）を求める。再生側と同じ決め方。
     * @param shot  ショット。
     * @param toEnd true なら終了、false なら開始。
     * @return ピントの距離（m）。
     */
    static float FocusDistance(const CameraShot &shot, bool toEnd);

    // ---- 再生位置の進行 ----

    /**
     * @brief ゲームでこの台本を流していれば、再生位置をムービーの再生位置に合わせる。
     * @param context エディタの文脈。
     * @return ゲームでこの台本を流している最中なら true。
     */
    bool FollowGamePlayback(EditorContext &context);

    // エディタ内の再生で、再生位置を実時間で進める。終わりに達したら、ループなら頭へ戻り、そうでなければ止める
    void StepPreviewPlayback();

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
    PreviewMode m_previewMode = PreviewMode::WhilePaused; // ゲーム画面への出し方
    bool m_previewPushed  = false; // ムービー用カメラへプレビューを渡しているか（止めるのを1回で済ませる）
    bool m_previewPlaying = false; // エディタ内の再生中か（一時停止のまま、再生位置を実時間で進める）
    bool m_previewLoop    = true;  // エディタ内の再生で、終わりに達したら頭へ戻すか
    bool m_followingGame  = false; // ゲームでこの台本を流している最中か（再生位置をそちらに合わせている）

    // タイムライン
    std::unique_ptr<CinematicTimelineSequence> m_sequence;          // 台本と ImSequencer のつなぎ
    float                                      m_time        = 0.0f; // 再生位置（秒）
    int                                        m_selectedRow = -1;   // 選んでいる行。下に数値を出す
    int                                        m_firstFrame  = 0;    // 横スクロールの位置（ImSequencer が書く）
};
