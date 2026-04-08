#include "FmodAudioManager.h"
#include <windows.h>

FmodAudioManager::~FmodAudioManager() {
    if (m_studioSystem) {
        m_studioSystem->unloadAll();
        m_studioSystem->release();
        m_studioSystem = nullptr;
    }
}

void FmodAudioManager::Initialize() {
    // 1. FMOD Studio システムの生成
    FMOD::Studio::System::create(&m_studioSystem);

    // 2. コアシステムの初期化（最大同時発音数 1024）
    // FMOD_INIT_NORMAL で標準的なオーディオ環境を構築
    m_studioSystem->initialize(1024, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr);
}

void FmodAudioManager::Update() {
    if (m_studioSystem) {
        // これを毎フレーム呼ぶことで、FMODが裏側でフェード処理や3D計算を行ってくれます
        m_studioSystem->update();
    }
}

void FmodAudioManager::LoadBank(const char* bankFilePath) {
    FMOD::Studio::Bank* bank = nullptr;
    FMOD_RESULT result = m_studioSystem->loadBankFile(bankFilePath, FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);

    if (result == FMOD_OK) {
        m_loadedBanks.push_back(bank);
    }
    else {
        OutputDebugStringA(("[FMOD] Failed to load bank: " + std::string(bankFilePath) + "\n").c_str());
    }
}

void FmodAudioManager::LoadEvent(const char* eventPath) {
    FMOD::Studio::EventDescription* desc = nullptr;
    FMOD_RESULT result = m_studioSystem->getEvent(eventPath, &desc);

    if (result == FMOD_OK) {
        // 文字列をハッシュ化して辞書に登録（O(1)アクセスの準備）
        uint32_t hash = CCL::Utils::HashString(eventPath);
        m_eventCache[hash] = desc;
    }
    else {
        OutputDebugStringA(("[FMOD] Failed to load event: " + std::string(eventPath) + "\n").c_str());
    }
}

void FmodAudioManager::PlayEvent(uint32_t eventHash) {
    auto it = m_eventCache.find(eventHash);
    if (it != m_eventCache.end()) {
        FMOD::Studio::EventInstance* inst = nullptr;
        it->second->createInstance(&inst);

        // 再生開始
        inst->start();

        // 【重要】release() を呼ぶと、再生が完了した瞬間にFMODが自動でメモリを破棄してくれます
        inst->release();
    }
}

void FmodAudioManager::PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3& position) {
    auto it = m_eventCache.find(eventHash);
    if (it != m_eventCache.end()) {
        FMOD::Studio::EventInstance* inst = nullptr;
        it->second->createInstance(&inst);

        // 3D属性（位置）をセット
        FMOD_3D_ATTRIBUTES attributes = { {0} };
        attributes.position = ToFMOD(position);
        attributes.forward = { 0, 0, 1 };
        attributes.up = { 0, 1, 0 };
        inst->set3DAttributes(&attributes);

        inst->start();
        inst->release();
    }
}

void FmodAudioManager::SetListenerPosition(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up) {
    FMOD_3D_ATTRIBUTES attributes = { {0} };
    attributes.position = ToFMOD(pos);
    attributes.forward = ToFMOD(forward);
    attributes.up = ToFMOD(up);

    // カメラの座標（耳の位置）をFMODに教える
    m_studioSystem->setListenerAttributes(0, &attributes);
}