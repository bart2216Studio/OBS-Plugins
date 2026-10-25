#pragma once

#include <obs.h>
#include <QChar>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <vector>

constexpr int kPresets = 6;           // marqueurs rapides
constexpr int kHotkeys = kPresets + 1; // 0 = marqueur principal, 1..6 = rapides

struct Marker {
	int index = 0;        // identifiant stable (ne change pas apres suppression)
	QDateTime wall;       // heure locale du marqueur
	qint64 streamMs = -1; // timecode depuis le debut du live (-1 = indisponible)
	qint64 recMs = -1;    // timecode depuis le debut de l'enregistrement
	QString title;
	QString clip;         // chemin du clip (replay buffer), vide si aucun
};

enum class RefMode { Auto = 0, Stream = 1, Record = 2 };

struct MarkerSettings {
	QString outDir;
	bool writeCsv = true;
	bool writeTxt = true;
	QChar csvSep = QLatin1Char(';');
	RefMode ref = RefMode::Auto;
	int rewindSec = 0;
	bool saveClip = true;
	QStringList presets;
};

class MarkerStore {
public:
	MarkerStore();

	MarkerSettings cfg;
	std::vector<Marker> markers;
	obs_hotkey_id hotkeys[kHotkeys];

	// --- session ---
	void newSession();
	bool hasSession() const { return !m_base.isEmpty(); }

	// --- chronologie live / enregistrement ---
	void streamStarted(quint64 ns) { m_streaming = true; m_streamStartNs = ns; }
	void streamStopped() { m_streaming = false; }
	void recordStarted(quint64 ns) { m_recording = true; m_recStartNs = ns; }
	void recordStopped() { m_recording = false; }
	bool streaming() const { return m_streaming; }
	bool recording() const { return m_recording; }

	// --- marqueurs ---
	int add(const QString &title, quint64 nowNs); // retourne l'identifiant
	void setClip(int id, const QString &path);
	void setTitle(int row, const QString &title);
	void removeAt(int row);

	qint64 refMs(const Marker &m) const;
	void write() const;
	QString csvPath() const;
	QString txtPath() const;

	// --- config ---
	void loadConfig();
	void saveConfig() const;

	static QString fmtTc(qint64 ms, bool withMs);
	static QString fmtYouTube(qint64 ms);

private:
	int m_nextId = 1;
	QString m_base; // nom de fichier sans extension
	bool m_streaming = false, m_recording = false;
	quint64 m_streamStartNs = 0, m_recStartNs = 0;
};
