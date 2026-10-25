/*
 * Marqueur de Chapitres & Clips Express — plugin OBS Studio
 * (c) 2026 bart_2216 Studio — licence MIT
 *
 * Un bouton (ou un raccourci) ajoute un marqueur de chapitre date, avec un
 * titre, dans un fichier CSV / TXT, et peut sauvegarder un clip via le
 * replay buffer d'OBS.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QMetaObject>
#include <QObject>

#include "dock.hpp"
#include "markers.hpp"

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Chapitres et Clips Express";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Marqueurs de chapitres dates + clips express (CSV / TXT) pour le montage.";
}

MODULE_EXPORT const char *obs_module_author(void)
{
	return "bart_2216 Studio";
}

static MarkerStore *g_store = nullptr;
static MarkerDock *g_dock = nullptr;

struct HotkeyData {
	int preset; // -1 = marqueur principal, 0..5 = marqueurs rapides
};
static HotkeyData g_hkData[kHotkeys];

static void hotkeyCb(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock)
		return;
	const int preset = static_cast<HotkeyData *>(data)->preset;
	const quint64 ts = os_gettime_ns(); // instant exact de l'appui
	QMetaObject::invokeMethod(
		g_dock,
		[preset, ts]() {
			if (g_dock)
				g_dock->hotkeyMarker(preset, ts);
		},
		Qt::QueuedConnection);
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!g_dock)
		return;
	const quint64 ts = os_gettime_ns();

	auto post = [](auto fn) { QMetaObject::invokeMethod(g_dock, fn, Qt::QueuedConnection); };

	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		post([ts]() { if (g_dock) g_dock->onStreamStarted(ts); });
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		post([]() { if (g_dock) g_dock->onStreamStopped(); });
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		post([ts]() { if (g_dock) g_dock->onRecordStarted(ts); });
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		post([]() { if (g_dock) g_dock->onRecordStopped(); });
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		post([]() { if (g_dock) g_dock->onReplaySaved(); });
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		post([]() { if (g_dock) g_dock->onReplayStopped(); });
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		g_dock->shutdown();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	g_store = new MarkerStore();

	// Raccourcis : 0 = marqueur (titre saisi), 1..6 = marqueurs rapides
	for (int i = 0; i < kHotkeys; i++) {
		g_hkData[i].preset = i - 1;
		const QByteArray id = QByteArrayLiteral("bart2216.chapter_markers.hk") + QByteArray::number(i);
		const QByteArray label = i == 0 ? QByteArrayLiteral("Chapitres : ajouter un marqueur")
						: QByteArrayLiteral("Chapitres : marqueur rapide ") + QByteArray::number(i);
		g_store->hotkeys[i] = obs_hotkey_register_frontend(id.constData(), label.constData(),
								    hotkeyCb, &g_hkData[i]);
	}
	g_store->loadConfig();

	g_dock = new MarkerDock(g_store);
	obs_frontend_add_dock_by_id("bart2216_chapter_markers", "Chapitres et Clips Express", g_dock);
	// Le dock est detruit avec la fenetre d'OBS avant obs_module_unload.
	QObject::connect(g_dock, &QObject::destroyed, [](QObject *) { g_dock = nullptr; });

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	blog(LOG_INFO, "[ChapterMarkers] plugin charge (bart_2216 Studio)");
	return true;
}

void obs_module_unload(void)
{
	// Fenetre/dock deja detruits ici ; les raccourcis sont liberes par OBS.
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	g_dock = nullptr;
}
