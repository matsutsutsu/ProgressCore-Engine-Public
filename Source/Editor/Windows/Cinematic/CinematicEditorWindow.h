#pragma once
// ===========================================================================
// 概要: ムービーの台本（Assets/Cinematics/*.json）を時間軸で見て編集するウィンドウ
//
// 責務:
//   - 台本を作る・開く・保存する。保存したらゲーム側に読み直させる
//   - ショットとトラックを ImSequencer の帯で並べ、ドラッグと数値の欄で直す
//   - 一時停止中、再生位置のカメラの絵をゲーム画面に出す（プレビュー）
//   - 編集中の写しを、本番と同じ仕組みで流す（保存しなくてよい）
//
// 備考: 編集はウィンドウが持つ写し（m_script）に対して行い、再生中のムービーの台本は触らない。Undo は持たない
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Editor/Windows/Cinematic/CinematicRipple.h"
#include "Engine/Cinematic/CinematicScript.h"

#include <cstddef>
#include <memory>
#include <string>

class CinematicTimelineSequence;

class CinematicEditorWindow : public EditorWindow
{
public:
    // 既定は非表示。Window メニューから開く
    CinematicEditorWindow();

    // CinematicTimelineSequence の定義はこのヘッダにないので、破棄も .cpp で行う
    ~CinematicEditorWindow() override;

protected:
    void DrawContents(EditorContext &context) override;

    // 閉じたフレームでも呼ばれる。閉じた瞬間にプレビューを止めないと、画面がプレビューのまま戻らない
    void OnFrameBegin(EditorContext &context) override;

private:
    // ---- 画面 ----

    void DrawToolbar(EditorContext &context);      // 新規・開く・保存と、開いている台本の名前
    void DrawTransportBar(EditorContext &context); // 再生の操作と、全体の長さ・スキップ可否
    void DrawStatusLine(EditorContext &context);   // 今の状態の注意か、直近の結果を1行だけ出す

    // 項目を足すボタン（再生位置に置く）と、リップルの切り替え。足したら true
    bool DrawAddButtons();

    // ショットとトラックの帯。帯を動かしたら true
    bool DrawTimeline();

    // 選んでいる項目の名前・消すボタン・数値の欄。値を変えたら true
    bool DrawInspector(EditorContext &context);

    // 選んでいる項目を台本から消し、選択を外す
    void EraseSelected();

    // ---- 種類ごとの数値の欄。値を変えたら true ----

    // ショットはリップルで後ろの項目を探すのに添字が要るので、添字で受ける
    bool DrawShotFields(EditorContext &context, std::size_t shotIndex);
    bool DrawShotDofFields(EditorContext &context, CameraShot &shot);
    bool DrawSoundFields(EditorContext &context, CinematicSound &sound);
    bool DrawEffectFields(EditorContext &context, CinematicEffect &effect);
    bool DrawSignalFields(CinematicSignal &signal);
    bool DrawAnimationFields(EditorContext &context, CinematicAnimation &anim);

    // 基準の名前の欄。シーンにいなければその場で知らせる
    // emptyMeans は空のときの意味（例: "ワールドの原点"）。nullptr なら空は許さない
    bool DrawSubjectField(EditorContext &context, std::string &subject, const char *emptyMeans);

    // ---- 再生とプレビュー ----

    // 編集中の写しを from 秒から流す。流している間だけ一時停止を解く（トラックはロジックの段で進むため）
    void StartPlayback(EditorContext &context, float from);

    // エディタから流したムービーをやめ、流す前に一時停止していたなら止め直す。sendStop なら停止要求も出す
    void StopPlayback(EditorContext &context, bool sendStop);

    // エディタから流したムービーの始まりと終わりを見る。終わったらループなら流し直す
    void UpdatePlayback(EditorContext &context);

    // ゲームでこの台本を流していれば、再生位置をそれに合わせて true を返す
    bool FollowGamePlayback(EditorContext &context);

    // ムービー用カメラへ、見せる台本と時刻を渡す。active が false なら止める
    void PushPreview(EditorContext &context, bool active);

    // 流す・見せるための写し。時刻の順に並べ直し、クリップのないアニメを外す
    CinematicScript MakePlayableCopy() const;

    // 再生中のムービーがこの台本かを見分ける名前。保存前の台本は仮の名前にする
    std::string PlaybackName() const;

    // ---- 操作 ----

    // 今の画面のカメラを、ショットの開始か終了（toEnd）の構図へ取り込む。できなければ false（理由は m_status）
    bool CaptureView(EditorContext &context, CameraShot &shot, bool toEnd);

    // 台本を読んで編集する写しにする。keepView なら再生位置を残す。読めなければ今の台本を残す
    void Open(const std::string &path, bool keepView);

    // 何もない台本にする。保存していない変更は捨てる
    void NewScript();

    // 今のパスへ保存し、読み直してゲーム側にも読み直させる。パスがなければ保存先を選ばせる
    bool Save(EditorContext &context);

    // 保存先を選んで保存する
    void SaveAs(EditorContext &context);

    // ---- 状態 ----

    CinematicScript m_script;        // 編集する写し。path が空なら、まだ保存していない台本
    bool            m_dirty = false; // 保存していない変更があるか
    std::string     m_status;        // 直近の読み込み・保存・取り込みの結果

    // --- プレビュー ---
    bool m_previewEnabled = true;  // 一時停止中に、再生位置の絵をゲーム画面へ出すか
    bool m_previewPushed  = false; // ムービー用カメラへプレビューを渡しているか
    bool m_followingGame  = false; // ゲームでこの台本を流している最中か

    // --- 再生 ---
    bool  m_playing          = false; // エディタから流したムービーが流れているか（流れ始め待ちも含む）
    bool  m_playStarted      = false; // そのムービーが実際に流れ始めたか
    int   m_playWaitFrames   = 0;     // 再生要求を出してから待ったフレーム数
    float m_playFrom         = 0.0f;  // 流し始めた位置（秒）。ループはここへ戻る
    bool  m_pausedBeforePlay = false; // 流す前に一時停止していたか
    bool  m_playLoop         = false; // 終わったら、流し始めた位置から流し直すか

    // --- タイムライン ---
    std::unique_ptr<CinematicTimelineSequence> m_sequence;          // 台本と ImSequencer のつなぎ
    float                                      m_time        = 0.0f; // 再生位置（秒）
    int                                        m_selectedRow = -1;   // 選んでいる行
    int                                        m_firstFrame  = 0;    // 横スクロールの位置（ImSequencer が書く）

    // --- リップル ---
    bool                      m_rippleEnabled   = true;  // ショットの終わりを動かしたとき、後ろの項目もずらすか
    bool                      m_timeFieldActive = false; // ショットの「開始」「長さ」の欄を触っている最中か
    CinematicRipple::Snapshot m_timeFieldRipple;         // その欄を触り始める直前の、後ろの項目の時刻
};
