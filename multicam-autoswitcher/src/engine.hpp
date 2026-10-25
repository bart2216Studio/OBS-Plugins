#pragma once

#include <obs.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

enum class SwitchMode { Strict = 0, Free = 1 };

struct Slot {
	std::string name;
	std::string audio; // nom de la source audio (micro)
	std::string scene; // nom de la scène cible

	// runtime
	obs_volmeter_t *volmeter = nullptr;
	std::atomic<float> level{-100.0f};
	bool speaking = false;
	double lastAbove = 0.0;
	double started = 0.0;
};

class Engine {
public:
	static constexpr int kMaxSlots = 16;

	Engine() = default;
	~Engine();

	// --- configuration ---
	bool enabled = false;
	SwitchMode mode = SwitchMode::Strict;
	double thresholdDb = -35.0;
	int holdMs = 1500;
	int releaseMs = 300;
	int prioMax = 0; // mode libre : index <= prioMax peuvent interrompre
	int silenceSec = 6;
	std::string defaultScene;
	obs_hotkey_id hotkey = OBS_INVALID_HOTKEY_ID;

	// index = position dans ce vecteur (0 = priorité absolue)
	std::vector<std::unique_ptr<Slot>> participants;

	bool addSlot();
	void removeSlot(int i);
	void swapSlots(int a, int b);

	void rebind(int i);
	void rebindAll();
	void unbindAll();

	// à appeler depuis le thread UI (~50 ms)
	void tick();

	int current() const { return m_current; }
	bool isSpeaking(int i) const;
	float level(int i) const;

	void load();
	void save() const;

private:
	void unbind(Slot &s);
	void switchTo(int idx, double now);

	int m_current = -1; // -1 = aucune / scène par défaut
	double m_lastSwitch = 0.0;
	double m_lastVoice = 0.0;
};
