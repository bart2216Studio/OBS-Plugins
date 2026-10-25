#pragma once

#include <QWidget>
#include <deque>

#include "markers.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class MarkerDock : public QWidget {
	Q_OBJECT

public:
	explicit MarkerDock(MarkerStore *store, QWidget *parent = nullptr);

	// appelees depuis plugin-main (thread UI)
	void hotkeyMarker(int preset, quint64 nowNs); // preset -1 = titre saisi
	void onStreamStarted(quint64 ns);
	void onStreamStopped();
	void onRecordStarted(quint64 ns);
	void onRecordStopped();
	void onReplaySaved();
	void onReplayStopped();
	void shutdown();

private slots:
	void onMarkClicked();
	void onTick();
	void onDelete();
	void onNewSession();
	void onBrowse();
	void onOpenFolder();

private:
	void addMarker(const QString &title, quint64 nowNs);
	void refreshTable();
	void applyOptions();
	void flash(const QString &text);

	MarkerStore *m_store;
	bool m_building = false;

	QLineEdit *m_title = nullptr;
	QPushButton *m_markBtn = nullptr;
	QCheckBox *m_clip = nullptr;
	QCheckBox *m_csv = nullptr;
	QCheckBox *m_txt = nullptr;
	QComboBox *m_ref = nullptr;
	QComboBox *m_sep = nullptr;
	QSpinBox *m_rewind = nullptr;
	QLineEdit *m_dir = nullptr;
	QTableWidget *m_table = nullptr;
	QLabel *m_status = nullptr;
	QLabel *m_note = nullptr;
	QLabel *m_file = nullptr;
	QTimer *m_timer = nullptr;

	std::deque<int> m_pendingClips; // identifiants de marqueurs en attente d'un clip
};
