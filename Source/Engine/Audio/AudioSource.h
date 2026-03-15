#pragma once

#include <memory>
#include <xaudio2.h>
#include "AudioResource.h"

// オーディオソース
class AudioSource
{
public:
	AudioSource(IXAudio2* xaudio, std::shared_ptr<AudioResource>& resource);
	~AudioSource();

	// 再生
	void Play(bool loop);

	// 停止
	void Stop();

	// 音量設定
	void SetVolume(float volume);

	// 再生中かどうか判定する関数宣言
    bool IsPlay();

private:
	IXAudio2SourceVoice*			sourceVoice = nullptr;
	std::shared_ptr<AudioResource>	resource;
};
