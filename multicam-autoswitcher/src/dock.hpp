#pragma once

#include <QWidget>
#include <QStringList>
#include <vector>

class Engine;
class LedWidget;
class QTableWidget;
class QPushButton;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QTimer;
class QProgressBar;

class MultiCamDock : public QWidget {
	Q_OBJECT

public:
	explicit MultiCamDock(Engine *engine, QWidget *parent = nullptr);

public slots:
	void refreshSources();
	void toggleMaster();
	void stopTimer();

private slots:
	void onTick();
	void onAddRow();
	void onRemoveRow();
	void onMoveUp();
	void onMoveDown();

private:
	void rebuildTable();
	void updateMasterButton();

	Engine *m_engine;
	bool m_building = false;

	QPushButton *m_master = nullptr;
	QComboBox *m_mode = nullptr;
	QLabel *m_status = nullptr;
	QTableWidget *m_table = nullptr;
	QDoubleSpinBox *m_threshold = nullptr;
	QSpinBox *m_hold = nullptr;
	QSpinBox *m_release = nullptr;
	QSpinBox *m_prio = nullptr;
	QComboBox *m_defaultScene = nullptr;
	QSpinBox *m_silence = nullptr;
	QTimer *m_timer = nullptr;

	std::vector<LedWidget *> m_leds;
	std::vector<QProgressBar *> m_bars;
	QStringList m_audioList;
	QStringList m_sceneList;
};
