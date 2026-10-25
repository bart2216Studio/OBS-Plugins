#include "markers.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/bmem.h>
#include <util/platform.h>

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

static QString defaultOutDir()
{
	QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	if (base.isEmpty())
		base = QDir::homePath();
	return QDir::toNativeSeparators(base + QStringLiteral("/OBS-Chapitres"));
}

MarkerStore::MarkerStore()
{
	for (int i = 0; i < kHotkeys; i++)
		hotkeys[i] = OBS_INVALID_HOTKEY_ID;
	cfg.outDir = defaultOutDir();
	cfg.presets = QStringList{QStringLiteral("Moment fort"), QStringLiteral("Fail"),
				  QStringLiteral("Drôle"),       QStringLiteral("Highlight"),
				  QStringLiteral("Changement de jeu"), QStringLiteral("Discussion")};
}

// --------------------------------------------------------------- formats
QString MarkerStore::fmtTc(qint64 ms, bool withMs)
{
	if (ms < 0)
		return QString();
	const qint64 h = ms / 3600000;
	const qint64 m = (ms / 60000) % 60;
	const qint64 s = (ms / 1000) % 60;
	const qint64 r = ms % 1000;
	QString out = QStringLiteral("%1:%2:%3")
			      .arg(h, 2, 10, QLatin1Char('0'))
			      .arg(m, 2, 10, QLatin1Char('0'))
			      .arg(s, 2, 10, QLatin1Char('0'));
	if (withMs)
		out += QStringLiteral(".%1").arg(r, 3, 10, QLatin1Char('0'));
	return out;
}

QString MarkerStore::fmtYouTube(qint64 ms)
{
	if (ms < 0)
		return QStringLiteral("--:--");
	const qint64 h = ms / 3600000;
	const qint64 m = (ms / 60000) % 60;
	const qint64 s = (ms / 1000) % 60;
	if (h > 0)
		return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
	return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

qint64 MarkerStore::refMs(const Marker &m) const
{
	switch (cfg.ref) {
	case RefMode::Stream:
		return m.streamMs;
	case RefMode::Record:
		return m.recMs;
	default:
		return m.streamMs >= 0 ? m.streamMs : m.recMs;
	}
}

// --------------------------------------------------------------- session
void MarkerStore::newSession()
{
	markers.clear();
	m_nextId = 1;
	m_base = QStringLiteral("chapitres_") +
		 QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

QString MarkerStore::csvPath() const
{
	return QDir(cfg.outDir).filePath(m_base + QStringLiteral(".csv"));
}

QString MarkerStore::txtPath() const
{
	return QDir(cfg.outDir).filePath(m_base + QStringLiteral(".txt"));
}

// -------------------------------------------------------------- marqueurs
int MarkerStore::add(const QString &title, quint64 nowNs)
{
	if (m_base.isEmpty())
		newSession();

	const qint64 back = (qint64)cfg.rewindSec * 1000;
	Marker m;
	m.index = m_nextId++;
	m.wall = QDateTime::currentDateTime().addMSecs(-back);
	if (m_streaming && nowNs >= m_streamStartNs)
		m.streamMs = std::max<qint64>(0, (qint64)((nowNs - m_streamStartNs) / 1000000ULL) - back);
	if (m_recording && nowNs >= m_recStartNs)
		m.recMs = std::max<qint64>(0, (qint64)((nowNs - m_recStartNs) / 1000000ULL) - back);
	m.title = title.trimmed().isEmpty() ? QStringLiteral("Marqueur %1").arg(m.index) : title.trimmed();
	markers.push_back(m);
	write();
	return m.index;
}

void MarkerStore::setClip(int id, const QString &path)
{
	for (auto &m : markers) {
		if (m.index == id) {
			m.clip = path;
			write();
			return;
		}
	}
}

void MarkerStore::setTitle(int row, const QString &title)
{
	if (row < 0 || row >= (int)markers.size())
		return;
	markers[row].title = title.trimmed();
	write();
}

void MarkerStore::removeAt(int row)
{
	if (row < 0 || row >= (int)markers.size())
		return;
	markers.erase(markers.begin() + row);
	write();
}

// ----------------------------------------------------------------- export
static QString csvField(const QString &s, QChar sep)
{
	const QChar quote = QLatin1Char('"');
	if (s.contains(sep) || s.contains(quote) || s.contains(QLatin1Char('\n')) ||
	    s.contains(QLatin1Char('\r'))) {
		QString t = s;
		t.replace(quote, QStringLiteral("\"\""));
		return quote + t + quote;
	}
	return s;
}

void MarkerStore::write() const
{
	if (m_base.isEmpty())
		return;
	QDir().mkpath(cfg.outDir);

	if (cfg.writeCsv) {
		const QChar sep = cfg.csvSep;
		QString out;
		const QStringList head{QStringLiteral("Index"), QStringLiteral("Date_Heure"),
				       QStringLiteral("Timecode_Live"), QStringLiteral("Timecode_Enregistrement"),
				       QStringLiteral("Secondes_Reference"), QStringLiteral("Titre"),
				       QStringLiteral("Clip")};
		out += head.join(sep) + QStringLiteral("\r\n");
		for (const Marker &m : markers) {
			const qint64 ref = refMs(m);
			QStringList row;
			row << QString::number(m.index)
			    << m.wall.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
			    << fmtTc(m.streamMs, true) << fmtTc(m.recMs, true)
			    << (ref >= 0 ? QString::number(ref / 1000.0, 'f', 3) : QString())
			    << csvField(m.title, sep) << csvField(m.clip, sep);
			out += row.join(sep) + QStringLiteral("\r\n");
		}
		QSaveFile f(csvPath());
		if (f.open(QIODevice::WriteOnly)) {
			f.write("\xEF\xBB\xBF"); // BOM UTF-8 (Excel)
			f.write(out.toUtf8());
			f.commit();
		}
	}

	if (cfg.writeTxt) {
		QString out;
		for (const Marker &m : markers)
			out += fmtYouTube(refMs(m)) + QLatin1Char(' ') + m.title + QStringLiteral("\r\n");
		QSaveFile f(txtPath());
		if (f.open(QIODevice::WriteOnly)) {
			f.write("\xEF\xBB\xBF");
			f.write(out.toUtf8());
			f.commit();
		}
	}
}

// ----------------------------------------------------------------- config
void MarkerStore::loadConfig()
{
	char *path = obs_module_config_path("config.json");
	if (!path)
		return;
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!d)
		return;

	obs_data_set_default_string(d, "out_dir", cfg.outDir.toUtf8().constData());
	obs_data_set_default_bool(d, "csv", true);
	obs_data_set_default_bool(d, "txt", true);
	obs_data_set_default_bool(d, "clip", true);
	obs_data_set_default_string(d, "csv_sep", ";");

	cfg.outDir = QString::fromUtf8(obs_data_get_string(d, "out_dir"));
	cfg.writeCsv = obs_data_get_bool(d, "csv");
	cfg.writeTxt = obs_data_get_bool(d, "txt");
	cfg.saveClip = obs_data_get_bool(d, "clip");
	cfg.csvSep = QString::fromUtf8(obs_data_get_string(d, "csv_sep")) == QStringLiteral(",")
			     ? QLatin1Char(',')
			     : QLatin1Char(';');
	const int ref = (int)obs_data_get_int(d, "ref");
	cfg.ref = ref == 1 ? RefMode::Stream : ref == 2 ? RefMode::Record : RefMode::Auto;
	cfg.rewindSec = std::clamp((int)obs_data_get_int(d, "rewind"), 0, 600);

	OBSDataArrayAutoRelease arr = obs_data_get_array(d, "presets");
	const size_t n = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < n && (int)i < kPresets; i++) {
		OBSDataAutoRelease it = obs_data_array_item(arr, i);
		const QString t = QString::fromUtf8(obs_data_get_string(it, "t")).trimmed();
		if (!t.isEmpty())
			cfg.presets[(int)i] = t;
	}

	for (int i = 0; i < kHotkeys; i++) {
		if (hotkeys[i] == OBS_INVALID_HOTKEY_ID)
			continue;
		const QByteArray key = QStringLiteral("hk_%1").arg(i).toUtf8();
		OBSDataArrayAutoRelease hk = obs_data_get_array(d, key.constData());
		if (hk)
			obs_hotkey_load(hotkeys[i], hk);
	}
}

void MarkerStore::saveConfig() const
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
	obs_data_set_string(d, "out_dir", cfg.outDir.toUtf8().constData());
	obs_data_set_bool(d, "csv", cfg.writeCsv);
	obs_data_set_bool(d, "txt", cfg.writeTxt);
	obs_data_set_bool(d, "clip", cfg.saveClip);
	obs_data_set_string(d, "csv_sep", cfg.csvSep == QLatin1Char(',') ? "," : ";");
	obs_data_set_int(d, "ref", (int)cfg.ref);
	obs_data_set_int(d, "rewind", cfg.rewindSec);

	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const QString &p : cfg.presets) {
		OBSDataAutoRelease it = obs_data_create();
		obs_data_set_string(it, "t", p.toUtf8().constData());
		obs_data_array_push_back(arr, it);
	}
	obs_data_set_array(d, "presets", arr);

	for (int i = 0; i < kHotkeys; i++) {
		if (hotkeys[i] == OBS_INVALID_HOTKEY_ID)
			continue;
		const QByteArray key = QStringLiteral("hk_%1").arg(i).toUtf8();
		OBSDataArrayAutoRelease hk = obs_hotkey_save(hotkeys[i]);
		obs_data_set_array(d, key.constData(), hk);
	}

	obs_data_save_json_safe(d, path, "tmp", "bak");
	bfree(path);
}
