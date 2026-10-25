#include "dock.hpp"
#include "engine.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

// ------------------------------------------------------------------- LED
class LedWidget : public QWidget {
public:
	explicit LedWidget(QWidget *parent = nullptr) : QWidget(parent)
	{
		setFixedSize(18, 18);
	}
	void setState(bool speaking, bool onAir)
	{
		if (speaking == m_speaking && onAir == m_onAir)
			return;
		m_speaking = speaking;
		m_onAir = onAir;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setBrush(m_speaking ? QColor(46, 204, 64) : QColor(75, 75, 75));
		p.setPen(m_onAir ? QPen(QColor(255, 59, 48), 3)
				 : QPen(QColor(30, 30, 30), 1));
		p.drawEllipse(rect().adjusted(2, 2, -2, -2));
	}

private:
	bool m_speaking = false;
	bool m_onAir = false;
};

// ------------------------------------------------------------ utilitaires
static QStringList listScenes()
{
	QStringList out;
	obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++)
		out << QString::fromUtf8(obs_source_get_name(list.sources.array[i]));
	obs_frontend_source_list_free(&list);
	return out;
}

static QStringList listAudioSources()
{
	QStringList out;
	obs_enum_sources(
		[](void *param, obs_source_t *src) -> bool {
			if (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO)
				static_cast<QStringList *>(param)->append(
					QString::fromUtf8(obs_source_get_name(src)));
			return true;
		},
		&out);
	return out;
}

static void fillCombo(QComboBox *cb, const QStringList &items, const QString &selected,
		      const QString &noneLabel)
{
	QSignalBlocker blocker(cb);
	cb->clear();
	cb->addItem(noneLabel, QString());
	for (const QString &s : items)
		cb->addItem(s, s);
	int idx = cb->findData(selected);
	if (idx < 0 && !selected.isEmpty()) { // source absente : on la garde visible
		cb->addItem(selected + QStringLiteral(" (introuvable)"), selected);
		idx = cb->count() - 1;
	}
	cb->setCurrentIndex(idx < 0 ? 0 : idx);
}

// ------------------------------------------------------------------- dock
MultiCamDock::MultiCamDock(Engine *engine, QWidget *parent)
	: QWidget(parent), m_engine(engine)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);

	// --- barre du haut : Master + mode
	auto *top = new QHBoxLayout();
	m_master = new QPushButton(this);
	m_master->setCheckable(true);
	m_master->setMinimumHeight(32);
	connect(m_master, &QPushButton::clicked, this, [this](bool on) {
		m_engine->enabled = on;
		updateMasterButton();
	});
	top->addWidget(m_master, 1);

	m_mode = new QComboBox(this);
	m_mode->addItem(QStringLiteral("Strict (interview / talk-show)"), 0);
	m_mode->addItem(QStringLiteral("Discussion libre (tour de rôle)"), 1);
	m_mode->setCurrentIndex(m_engine->mode == SwitchMode::Free ? 1 : 0);
	top->addWidget(m_mode, 2);
	root->addLayout(top);

	m_status = new QLabel(this);
	root->addWidget(m_status);

	// --- tableau de mapping
	m_table = new QTableWidget(0, 6, this);
	m_table->setHorizontalHeaderLabels({QStringLiteral(""), QStringLiteral("Idx"),
					    QStringLiteral("Nom"), QStringLiteral("Micro"),
					    QStringLiteral("Scène / Caméra"),
					    QStringLiteral("Niveau")});
	m_table->verticalHeader()->setVisible(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	auto *h = m_table->horizontalHeader();
	h->setSectionResizeMode(0, QHeaderView::Fixed);
	h->setSectionResizeMode(1, QHeaderView::Fixed);
	h->setSectionResizeMode(2, QHeaderView::Stretch);
	h->setSectionResizeMode(3, QHeaderView::Stretch);
	h->setSectionResizeMode(4, QHeaderView::Stretch);
	h->setSectionResizeMode(5, QHeaderView::Fixed);
	m_table->setColumnWidth(0, 30);
	m_table->setColumnWidth(1, 32);
	m_table->setColumnWidth(5, 80);
	root->addWidget(m_table, 1);

	auto *rowBtns = new QHBoxLayout();
	auto *add = new QPushButton(QStringLiteral("+ Participant"), this);
	auto *rem = new QPushButton(QStringLiteral("− Retirer"), this);
	auto *up = new QPushButton(QStringLiteral("▲"), this);
	auto *down = new QPushButton(QStringLiteral("▼"), this);
	up->setToolTip(QStringLiteral("Monter (plus prioritaire)"));
	down->setToolTip(QStringLiteral("Descendre (moins prioritaire)"));
	connect(add, &QPushButton::clicked, this, &MultiCamDock::onAddRow);
	connect(rem, &QPushButton::clicked, this, &MultiCamDock::onRemoveRow);
	connect(up, &QPushButton::clicked, this, &MultiCamDock::onMoveUp);
	connect(down, &QPushButton::clicked, this, &MultiCamDock::onMoveDown);
	rowBtns->addWidget(add);
	rowBtns->addWidget(rem);
	rowBtns->addStretch(1);
	rowBtns->addWidget(up);
	rowBtns->addWidget(down);
	root->addLayout(rowBtns);

	// --- paramètres
	auto *box = new QGroupBox(QStringLiteral("Réglages"), this);
	auto *form = new QFormLayout(box);

	m_threshold = new QDoubleSpinBox(box);
	m_threshold->setRange(-70.0, -5.0);
	m_threshold->setSuffix(QStringLiteral(" dB"));
	m_threshold->setValue(m_engine->thresholdDb);
	m_threshold->setToolTip(QStringLiteral("Seuil de déclenchement (noise gate)"));
	form->addRow(QStringLiteral("Seuil de déclenchement"), m_threshold);

	m_hold = new QSpinBox(box);
	m_hold->setRange(0, 10000);
	m_hold->setSingleStep(100);
	m_hold->setSuffix(QStringLiteral(" ms"));
	m_hold->setValue(m_engine->holdMs);
	m_hold->setToolTip(QStringLiteral("Temps minimal avant un changement non prioritaire"));
	form->addRow(QStringLiteral("Délai de maintien"), m_hold);

	m_release = new QSpinBox(box);
	m_release->setRange(0, 2000);
	m_release->setSingleStep(50);
	m_release->setSuffix(QStringLiteral(" ms"));
	m_release->setValue(m_engine->releaseMs);
	form->addRow(QStringLiteral("Relâchement de la voix"), m_release);

	m_prio = new QSpinBox(box);
	m_prio->setRange(0, Engine::kMaxSlots - 1);
	m_prio->setValue(m_engine->prioMax);
	m_prio->setToolTip(QStringLiteral("Mode libre : les index ≤ cette valeur peuvent interrompre"));
	form->addRow(QStringLiteral("Index prioritaire max (libre)"), m_prio);

	m_defaultScene = new QComboBox(box);
	form->addRow(QStringLiteral("Scène par défaut (silence)"), m_defaultScene);

	m_silence = new QSpinBox(box);
	m_silence->setRange(1, 300);
	m_silence->setSuffix(QStringLiteral(" s"));
	m_silence->setValue(m_engine->silenceSec);
	form->addRow(QStringLiteral("Silence avant scène par défaut"), m_silence);
	root->addWidget(box);

	auto *credit = new QLabel(QStringLiteral("<small>bart_2216 Studio · v1.0.0</small>"), this);
	credit->setAlignment(Qt::AlignRight);
	root->addWidget(credit);

	// --- connexions des réglages
	connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int) {
		m_engine->mode = m_mode->currentData().toInt() == 1 ? SwitchMode::Free
								     : SwitchMode::Strict;
		m_prio->setEnabled(m_engine->mode == SwitchMode::Free);
		m_engine->save();
	});
	connect(m_threshold, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		m_engine->thresholdDb = v;
		m_engine->save();
	});
	connect(m_hold, &QSpinBox::valueChanged, this, [this](int v) {
		m_engine->holdMs = v;
		m_engine->save();
	});
	connect(m_release, &QSpinBox::valueChanged, this, [this](int v) {
		m_engine->releaseMs = v;
		m_engine->save();
	});
	connect(m_prio, &QSpinBox::valueChanged, this, [this](int v) {
		m_engine->prioMax = v;
		m_engine->save();
	});
	connect(m_silence, &QSpinBox::valueChanged, this, [this](int v) {
		m_engine->silenceSec = v;
		m_engine->save();
	});
	connect(m_defaultScene, &QComboBox::currentIndexChanged, this, [this](int) {
		if (m_building)
			return;
		m_engine->defaultScene = m_defaultScene->currentData().toString().toStdString();
		m_engine->save();
	});

	m_prio->setEnabled(m_engine->mode == SwitchMode::Free);
	updateMasterButton();

	m_timer = new QTimer(this);
	m_timer->setInterval(50);
	connect(m_timer, &QTimer::timeout, this, &MultiCamDock::onTick);
	m_timer->start();

	refreshSources();
}

void MultiCamDock::stopTimer()
{
	if (m_timer)
		m_timer->stop();
}

void MultiCamDock::updateMasterButton()
{
	QSignalBlocker b(m_master);
	m_master->setChecked(m_engine->enabled);
	if (m_engine->enabled) {
		m_master->setText(QStringLiteral("● AUTO-RÉALISATION : ON"));
		m_master->setStyleSheet(QStringLiteral(
			"QPushButton{background:#1e8e3e;color:white;font-weight:bold;}"));
	} else {
		m_master->setText(QStringLiteral("○ AUTO-RÉALISATION : OFF"));
		m_master->setStyleSheet(QStringLiteral(
			"QPushButton{background:#555;color:#ddd;font-weight:bold;}"));
	}
}

void MultiCamDock::toggleMaster()
{
	m_engine->enabled = !m_engine->enabled;
	updateMasterButton();
}

void MultiCamDock::refreshSources()
{
	m_audioList = listAudioSources();
	m_sceneList = listScenes();

	m_building = true;
	fillCombo(m_defaultScene, m_sceneList, QString::fromStdString(m_engine->defaultScene),
		  QStringLiteral("(aucune)"));
	m_building = false;
	rebuildTable();
}

void MultiCamDock::rebuildTable()
{
	const int keepRow = m_table->currentRow();
	m_building = true;
	m_leds.clear();
	m_bars.clear();
	m_table->setRowCount(0);
	m_table->setRowCount((int)m_engine->participants.size());

	for (int i = 0; i < (int)m_engine->participants.size(); i++) {
		Slot &s = *m_engine->participants[i];

		// LED (centrée)
		auto *cell = new QWidget();
		auto *cl = new QHBoxLayout(cell);
		cl->setContentsMargins(0, 0, 0, 0);
		cl->setAlignment(Qt::AlignCenter);
		auto *led = new LedWidget(cell);
		cl->addWidget(led);
		m_table->setCellWidget(i, 0, cell);
		m_leds.push_back(led);

		// Index
		auto *idx = new QTableWidgetItem(QString::number(i));
		idx->setTextAlignment(Qt::AlignCenter);
		m_table->setItem(i, 1, idx);

		// Nom
		auto *name = new QLineEdit(QString::fromStdString(s.name));
		connect(name, &QLineEdit::textEdited, this, [this, i](const QString &t) {
			if (i < (int)m_engine->participants.size()) {
				m_engine->participants[i]->name = t.toStdString();
				m_engine->save();
			}
		});
		m_table->setCellWidget(i, 2, name);

		// Micro
		auto *audio = new QComboBox();
		fillCombo(audio, m_audioList, QString::fromStdString(s.audio),
			  QStringLiteral("(aucun)"));
		connect(audio, &QComboBox::currentIndexChanged, this, [this, i, audio](int) {
			if (m_building || i >= (int)m_engine->participants.size())
				return;
			m_engine->participants[i]->audio = audio->currentData().toString().toStdString();
			m_engine->rebind(i);
			m_engine->save();
		});
		m_table->setCellWidget(i, 3, audio);

		// Scène
		auto *scene = new QComboBox();
		fillCombo(scene, m_sceneList, QString::fromStdString(s.scene),
			  QStringLiteral("(aucune)"));
		connect(scene, &QComboBox::currentIndexChanged, this, [this, i, scene](int) {
			if (m_building || i >= (int)m_engine->participants.size())
				return;
			m_engine->participants[i]->scene = scene->currentData().toString().toStdString();
			m_engine->save();
		});
		m_table->setCellWidget(i, 4, scene);

		// Niveau
		auto *bar = new QProgressBar();
		bar->setRange(0, 100);
		bar->setTextVisible(false);
		bar->setMaximumHeight(10);
		m_table->setCellWidget(i, 5, bar);
		m_bars.push_back(bar);
	}
	m_building = false;
	if (keepRow >= 0 && keepRow < m_table->rowCount())
		m_table->selectRow(keepRow);
}

void MultiCamDock::onAddRow()
{
	if (m_engine->addSlot()) {
		m_engine->save();
		rebuildTable();
	}
}

void MultiCamDock::onRemoveRow()
{
	const int r = m_table->currentRow();
	if (r < 0)
		return;
	m_engine->removeSlot(r);
	m_engine->save();
	rebuildTable();
}

void MultiCamDock::onMoveUp()
{
	const int r = m_table->currentRow();
	if (r <= 0)
		return;
	m_engine->swapSlots(r, r - 1);
	m_engine->save();
	rebuildTable();
	m_table->selectRow(r - 1);
}

void MultiCamDock::onMoveDown()
{
	const int r = m_table->currentRow();
	if (r < 0 || r >= (int)m_engine->participants.size() - 1)
		return;
	m_engine->swapSlots(r, r + 1);
	m_engine->save();
	rebuildTable();
	m_table->selectRow(r + 1);
}

void MultiCamDock::onTick()
{
	m_engine->tick();

	const int cur = m_engine->current();
	for (int i = 0; i < (int)m_leds.size() && i < (int)m_engine->participants.size(); i++) {
		m_leds[i]->setState(m_engine->isSpeaking(i), cur == i);
		const float db = m_engine->level(i);
		const int v = (int)std::clamp((db + 60.0f) / 60.0f * 100.0f, 0.0f, 100.0f);
		m_bars[i]->setValue(v);
	}

	QString txt;
	if (cur >= 0 && cur < (int)m_engine->participants.size()) {
		const QString n = QString::fromStdString(m_engine->participants[cur]->name);
		txt = QStringLiteral("À l'antenne : [%1] %2").arg(cur).arg(n);
	} else {
		txt = QStringLiteral("À l'antenne : —");
	}
	m_status->setText(txt);
}
