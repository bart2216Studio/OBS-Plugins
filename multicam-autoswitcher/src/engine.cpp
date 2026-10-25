#include "engine.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/bmem.h>
#include <util/platform.h>

#include <algorithm>

// ----------------------------------------------------------------- volmeter
static void volCb(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
		  const float peak[MAX_AUDIO_CHANNELS],
		  const float input_peak[MAX_AUDIO_CHANNELS])
{
	UNUSED_PARAMETER(peak);
	UNUSED_PARAMETER(input_peak);
	Slot *s = static_cast<Slot *>(param);
	int nr = s->volmeter ? obs_volmeter_get_nr_channels(s->volmeter) : 0;
	if (nr > MAX_AUDIO_CHANNELS)
		nr = MAX_AUDIO_CHANNELS;
	float m = -100.0f;
	for (int c = 0; c < nr; c++)
		m = std::max(m, magnitude[c]);
	s->level.store(m, std::memory_order_relaxed);
}

Engine::~Engine()
{
	unbindAll();
}

void Engine::unbind(Slot &s)
{
	if (!s.volmeter)
		return;
	obs_volmeter_remove_callback(s.volmeter, volCb, &s);
	obs_volmeter_detach_source(s.volmeter);
	obs_volmeter_destroy(s.volmeter);
	s.volmeter = nullptr;
	s.level.store(-100.0f);
	s.speaking = false;
}

void Engine::rebind(int i)
{
	if (i < 0 || i >= (int)participants.size())
		return;
	Slot &s = *participants[i];
	unbind(s);
	if (s.audio.empty())
		return;
	OBSSourceAutoRelease src = obs_get_source_by_name(s.audio.c_str());
	if (!src)
		return;
	s.volmeter = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_add_callback(s.volmeter, volCb, &s);
	if (!obs_volmeter_attach_source(s.volmeter, src))
		unbind(s);
}

void Engine::rebindAll()
{
	for (int i = 0; i < (int)participants.size(); i++)
		rebind(i);
}

void Engine::unbindAll()
{
	for (auto &s : participants)
		unbind(*s);
}

bool Engine::addSlot()
{
	if ((int)participants.size() >= kMaxSlots)
		return false;
	participants.push_back(std::make_unique<Slot>());
	participants.back()->name = "Participant " + std::to_string(participants.size() - 1);
	return true;
}

void Engine::removeSlot(int i)
{
	if (i < 0 || i >= (int)participants.size() || participants.size() <= 1)
		return;
	unbind(*participants[i]);
	participants.erase(participants.begin() + i);
	m_current = -1;
}

void Engine::swapSlots(int a, int b)
{
	if (a < 0 || b < 0 || a >= (int)participants.size() || b >= (int)participants.size())
		return;
	std::swap(participants[a], participants[b]);
	m_current = -1;
}

bool Engine::isSpeaking(int i) const
{
	return i >= 0 && i < (int)participants.size() && participants[i]->speaking;
}

float Engine::level(int i) const
{
	return (i >= 0 && i < (int)participants.size()) ? participants[i]->level.load() : -100.0f;
}

// ------------------------------------------------------------------ logique
void Engine::switchTo(int idx, double now)
{
	const std::string &name = idx < 0 ? defaultScene : participants[idx]->scene;
	if (name.empty())
		return;
	OBSSourceAutoRelease src = obs_get_source_by_name(name.c_str());
	if (!src)
		return;
	obs_frontend_set_current_scene(src);
	m_current = idx;
	m_lastSwitch = now;
}

void Engine::tick()
{
	const double now = os_gettime_ns() / 1e9;
	const int n = (int)participants.size();
	const double release = releaseMs / 1000.0;

	// 1) Détection de voix (noise gate + relâchement) : toujours active,
	//    pour que les voyants servent aussi à calibrer le seuil.
	for (int i = 0; i < n; i++) {
		Slot &s = *participants[i];
		if (!s.volmeter) {
			s.speaking = false;
			continue;
		}
		if (s.level.load(std::memory_order_relaxed) > thresholdDb) {
			s.lastAbove = now;
			if (!s.speaking) {
				s.speaking = true;
				s.started = now;
			}
		} else if (s.speaking && (now - s.lastAbove) > release) {
			s.speaking = false;
		}
	}

	if (!enabled)
		return;

	std::vector<int> speakers;
	for (int i = 0; i < n; i++)
		if (participants[i]->speaking && !participants[i]->scene.empty())
			speakers.push_back(i);

	// 2) Silence prolongé -> scène par défaut
	if (speakers.empty()) {
		if (!defaultScene.empty() && m_current >= 0 &&
		    (now - m_lastVoice) > silenceSec)
			switchTo(-1, now);
		return;
	}
	m_lastVoice = now;

	const bool holdOk = (now - m_lastSwitch) >= holdMs / 1000.0;
	const int best = *std::min_element(speakers.begin(), speakers.end());
	int target = -1;
	bool doSwitch = false;

	if (mode == SwitchMode::Strict) {
		if (m_current < 0) {
			target = best; doSwitch = true;
		} else if (best < m_current) {
			target = best; doSwitch = true; // priorité : immédiat
		} else if (!participants[m_current]->speaking && holdOk) {
			target = best; doSwitch = true;
		}
	} else {
		std::vector<int> prio;
		for (int i : speakers)
			if (i <= prioMax)
				prio.push_back(i);
		const int earliest = *std::min_element(
			speakers.begin(), speakers.end(), [&](int a, int b) {
				return participants[a]->started < participants[b]->started;
			});
		if (m_current < 0) {
			target = prio.empty() ? earliest
					      : *std::min_element(prio.begin(), prio.end());
			doSwitch = true;
		} else if (!prio.empty() &&
			   *std::min_element(prio.begin(), prio.end()) < m_current) {
			target = *std::min_element(prio.begin(), prio.end());
			doSwitch = true; // index prioritaire : immédiat
		} else if (!participants[m_current]->speaking && holdOk) {
			target = earliest; doSwitch = true;
		}
	}

	if (doSwitch && target != m_current)
		switchTo(target, now);
}

// ------------------------------------------------------------ persistance
void Engine::load()
{
	char *path = obs_module_config_path("config.json");
	if (!path)
		return;
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);

	participants.clear();
	if (d) {
		obs_data_set_default_int(d, "mode", 0);
		obs_data_set_default_double(d, "threshold", -35.0);
		obs_data_set_default_int(d, "hold_ms", 1500);
		obs_data_set_default_int(d, "release_ms", 300);
		obs_data_set_default_int(d, "prio_max", 0);
		obs_data_set_default_int(d, "silence_s", 6);

		mode = obs_data_get_int(d, "mode") == 1 ? SwitchMode::Free
							 : SwitchMode::Strict;
		thresholdDb = obs_data_get_double(d, "threshold");
		holdMs = (int)obs_data_get_int(d, "hold_ms");
		releaseMs = (int)obs_data_get_int(d, "release_ms");
		prioMax = (int)obs_data_get_int(d, "prio_max");
		silenceSec = (int)obs_data_get_int(d, "silence_s");
		defaultScene = obs_data_get_string(d, "default_scene");

		OBSDataArrayAutoRelease arr = obs_data_get_array(d, "participants");
		const size_t cnt = arr ? obs_data_array_count(arr) : 0;
		for (size_t i = 0; i < cnt && (int)i < kMaxSlots; i++) {
			OBSDataAutoRelease it = obs_data_array_item(arr, i);
			auto s = std::make_unique<Slot>();
			s->name = obs_data_get_string(it, "name");
			s->audio = obs_data_get_string(it, "audio");
			s->scene = obs_data_get_string(it, "scene");
			participants.push_back(std::move(s));
		}

		if (hotkey != OBS_INVALID_HOTKEY_ID) {
			OBSDataArrayAutoRelease hk = obs_data_get_array(d, "hotkey");
			if (hk)
				obs_hotkey_load(hotkey, hk);
		}
	}
	if (participants.empty())
		for (int i = 0; i < 3; i++)
			addSlot();
	enabled = false; // sécurité : toujours OFF au démarrage
}

void Engine::save() const
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_config_path("config.json");
	if (!path)
		return;

	OBSDataAutoRelease d = obs_data_create();
	obs_data_set_int(d, "mode", (int)mode);
	obs_data_set_double(d, "threshold", thresholdDb);
	obs_data_set_int(d, "hold_ms", holdMs);
	obs_data_set_int(d, "release_ms", releaseMs);
	obs_data_set_int(d, "prio_max", prioMax);
	obs_data_set_int(d, "silence_s", silenceSec);
	obs_data_set_string(d, "default_scene", defaultScene.c_str());

	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const auto &s : participants) {
		OBSDataAutoRelease it = obs_data_create();
		obs_data_set_string(it, "name", s->name.c_str());
		obs_data_set_string(it, "audio", s->audio.c_str());
		obs_data_set_string(it, "scene", s->scene.c_str());
		obs_data_array_push_back(arr, it);
	}
	obs_data_set_array(d, "participants", arr);

	if (hotkey != OBS_INVALID_HOTKEY_ID) {
		OBSDataArrayAutoRelease hk = obs_hotkey_save(hotkey);
		obs_data_set_array(d, "hotkey", hk);
	}

	obs_data_save_json_safe(d, path, "tmp", "bak");
	bfree(path);
}
