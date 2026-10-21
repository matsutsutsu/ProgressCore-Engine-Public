#pragma once
// ===========================================================================
// 概要: 遊ぶ人が設定画面で変える値（視点の感度・BGM と効果音の音量）を持ち、ファイルへ保存する。
//
// 責務:
//   - 設定の値をアプリ全体で1つだけ持つ（World ではないので、シーンを切り替えても残る）
//   - 最初に使うときにファイルから読み、設定画面を閉じたときに書く
//
// 備考:
//   値をカメラと音声へ反映するのは SettingsScreenSystem。全シーンで毎フレーム反映する。
//
//   保存先は、作業フォルダの Saved/GameSettings.json。配布する Assets とは分ける。
//
//   ファイルが無い・壊れているときは、既定値のまま使う。
// ===========================================================================

struct GameSettings
{
    float lookSensitivity = 1.0f; // 視点の感度の倍率（マウス・スティック共通）
    float bgmVolume       = 0.5f; // BGM の音量（0〜1）
    float sfxVolume       = 0.5f; // 効果音の音量（0〜1）

    // --- 範囲と、設定画面の左右の入力で変える1段の刻み ---
    static constexpr float kSensitivityMin  = 0.25f; // 感度の最小
    static constexpr float kSensitivityMax  = 2.0f;  // 感度の最大
    static constexpr float kSensitivityStep = 0.05f; // 感度の刻み
    static constexpr float kVolumeStep      = 0.1f;  // 音量の刻み

    // アプリ全体で1つの設定。最初に呼んだときにファイルから読む
    static GameSettings &Instance();

    // ファイルへ書く（設定画面を閉じたときに呼ぶ）
    void Save() const;

private:
    void Load(); // ファイルから読む（無ければ既定値のまま）
};
