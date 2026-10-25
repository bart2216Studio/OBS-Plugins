/*
 * Multi-Camera Auto-Switcher — plugin OBS Studio
 * (c) 2026 bart_2216 Studio — licence MIT
 *
 * Réalisateur virtuel : bascule la scène selon le micro actif, avec une
 * hiérarchie stricte par index (0 = priorité absolue).
 */
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMetaObject>
#include <QObject>

#include "engine.hpp"
#include "dock.hpp"

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Multi-Camera Auto-Switcher";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Bascule automatiquement de caméra selon le micro actif (priorité par index).";
}

MODULE_EXPORT const char *obs_module_author(void)
{
	return "bart_2216 Studio";
}

static Engine *g_engine = nullptr;
static MultiCamDock *g_dock = nullptr;

static void hotkeyCb(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_dock)
		QMetaObject::invokeMethod(g_dock, "toggleMaster", Qt::QueuedConnection);
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!g_engine || !g_dock)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		g_engine->rebindAll();
		QMetaObject::invokeMethod(g_dock, "refreshSources", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		QMetaObject::invokeMethod(g_dock, "refreshSources", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		g_engine->enabled = false;
		g_engine->unbindAll();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		QMetaObject::invokeMethod(g_dock, "stopTimer", Qt::DirectConnection);
		g_engine->enabled = false;
		g_engine->save();
		g_engine->unbindAll();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	g_engine = new Engine();
	g_engine->hotkey = obs_hotkey_register_frontend(
		"bart2216.multicam_autoswitcher.toggle",
		"Multi-Camera Auto-Switcher : ON/OFF", hotkeyCb, nullptr);
	g_engine->load();

	g_dock = new MultiCamDock(g_engine);
	obs_frontend_add_dock_by_id("bart2216_multicam_autoswitcher",
				    "Multi-Camera Auto-Switcher", g_dock);
	// Le dock appartient a la fenetre principale d'OBS et est detruit avec elle,
	// AVANT obs_module_unload : on oublie le pointeur des qu'il est detruit.
	QObject::connect(g_dock, &QObject::destroyed, [](QObject *) { g_dock = nullptr; });

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	blog(LOG_INFO, "[MultiCam] plugin chargé (bart_2216 Studio)");
	return true;
}

void obs_module_unload(void)
{
	// A ce stade la fenetre d'OBS (donc le dock) est deja detruite :
	// ne JAMAIS toucher a g_dock ici. Les raccourcis sont liberes par OBS.
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	if (g_engine)
		g_engine->unbindAll();
}
