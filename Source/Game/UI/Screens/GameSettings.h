#pragma once
// ===========================================================================
// 概要: 遊ぶ人が設定画面で変える値（視点の感度・上下反転・BGM と効果音の音量）を持ち、ファイルへ保存する。
//
// 責務:
//   - 設定の値を1か所に持つ（シーンを切り替えても残るよう、World ではなくアプリ全体で1つ）
//   - 起動して最初に使うときにファイルから読み、設定画面を閉じたときに書く
//
// 備考: 値をカメラと音声へ反映するのは SettingsScreenSystem（全シーンで毎フレーム反映する）。
//       保存先は Build/User/GameSettings.json（エディタの設定と同じ場所。ゲームの資産とは分ける）。
//       ファイルが無い・壊れているときは既定値のまま使う。
// ===========================================================================

struct GameSettings
{
    float lookSensitivity = 1.0f; // 視点の感度の倍率（マウス・スティック共通）
    bool  invertY         = false; // 視点の上下反転
    float bgmVolume       = 0.8f;  // BGM の音量（0〜1）
    float sfxVolume       = 1.0f;  // 効果音の音量（0〜1）

    // 範囲と1段の刻み（設定画面の左右の入力で、この刻みで変える）
    static constexpr float kSensitivityMin  = 0.25f;
    static constexpr float kSensitivityMax  = 2.0f;
    static constexpr float kSensitivityStep = 0.05f;
    static constexpr float kVolumeStep      = 0.1f;

    // アプリ全体で1つの設定。最初に呼んだときにファイルから読む
    static GameSettings &Instance();

    // ファイルへ書く（設定画面を閉じたときに呼ぶ）
    void Save() const;

private:
    void Load(); // ファイルから読む（無ければ既定値のまま）
};
