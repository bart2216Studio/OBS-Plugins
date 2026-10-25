#include "dock.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

MarkerDock::MarkerDock(MarkerStore *store, QWidget *parent) : QWidget(parent), m_store(store)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);

	// ---- saisie du titre + bouton principal
	auto *top = new QHBoxLayout();
	m_title = new QLineEdit(this);
	m_title->setPlaceholderText(QStringLiteral("Titre du chapitre (optionnel)…"));
	m_markBtn = new QPushButton(QStringLiteral("📍 Marquer"), this);
	m_markBtn->setMinimumHeight(32);
	m_markBtn->setStyleSheet(QStringLiteral("QPushButton{background:#1e6fd9;color:white;font-weight:bold;}"));
	top->addWidget(m_title, 1);
	top->addWidget(m_markBtn);
	root->addLayout(top);

	m_clip = new QCheckBox(QStringLiteral("Sauvegarder aussi un clip (replay buffer)"), this);
	m_clip->setChecked(m_store->cfg.saveClip);
	root->addWidget(m_clip);

	// ---- marqueurs rapides
	auto *grid = new QGridLayout();
	for (int i = 0; i < kPresets; i++) {
		auto *b = new QPushButton(m_store->cfg.presets[i], this);
		b->setToolTip(QStringLiteral("Clic : ajoute ce marqueur. Clic droit : renommer.\n"
					     "Raccourci : Paramètres → Raccourcis clavier."));
		b->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(b, &QPushButton::clicked, this, [this, i]() {
			addMarker(m_store->cfg.presets[i], os_gettime_ns());
		});
		connect(b, &QPushButton::customContextMenuRequested, this, [this, i, b](const QPoint &) {
			bool ok = false;
			const QString t = QInputDialog::getText(this, QStringLiteral("Renommer le marqueur rapide"),
								QStringLiteral("Titre :"), QLineEdit::Normal,
								m_store->cfg.presets[i], &ok);
			if (ok && !t.trimmed().isEmpty()) {
				m_store->cfg.presets[i] = t.trimmed();
				b->setText(t.trimmed());
				m_store->saveConfig();
			}
		});
		grid->addWidget(b, i / 3, i % 3);
	}
	root->addLayout(grid);

	m_status = new QLabel(this);
	m_status->setTextFormat(Qt::RichText);
	root->addWidget(m_status);
	m_note = new QLabel(this);
	m_note->setStyleSheet(QStringLiteral("color:#f5a623;"));
	root->addWidget(m_note);

	// ---- tableau
	m_table = new QTableWidget(0, 6, this);
	m_table->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Heure"),
					    QStringLiteral("TC Live"), QStringLiteral("TC Enreg."),
					    QStringLiteral("Titre"), QStringLiteral("Clip")});
	m_table->verticalHeader()->setVisible(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	auto *h = m_table->horizontalHeader();
	h->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	h->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	h->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	h->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	h->setSectionResizeMode(4, QHeaderView::Stretch);
	h->setSectionResizeMode(5, QHeaderView::ResizeToContents);
	root->addWidget(m_table, 1);

	auto *rowBtns = new QHBoxLayout();
	auto *del = new QPushButton(QStringLiteral("Supprimer"), this);
	auto *newS = new QPushButton(QStringLiteral("Nouvelle session"), this);
	newS->setToolTip(QStringLiteral("Démarre un nouveau fichier (la liste est vidée, les anciens fichiers restent sur le disque)."));
	rowBtns->addWidget(del);
	rowBtns->addWidget(newS);
	rowBtns->addStretch(1);
	root->addLayout(rowBtns);

	// ---- options
	auto *box = new QGroupBox(QStringLiteral("Export"), this);
	auto *g = new QGridLayout(box);

	m_csv = new QCheckBox(QStringLiteral("CSV"), box);
	m_csv->setChecked(m_store->cfg.writeCsv);
	m_txt = new QCheckBox(QStringLiteral("TXT (chapitres YouTube)"), box);
	m_txt->setChecked(m_store->cfg.writeTxt);
	m_sep = new QComboBox(box);
	m_sep->addItem(QStringLiteral("Séparateur ;"), QStringLiteral(";"));
	m_sep->addItem(QStringLiteral("Séparateur ,"), QStringLiteral(","));
	m_sep->setCurrentIndex(m_store->cfg.csvSep == QLatin1Char(',') ? 1 : 0);
	g->addWidget(m_csv, 0, 0);
	g->addWidget(m_sep, 0, 1);
	g->addWidget(m_txt, 0, 2);

	m_ref = new QComboBox(box);
	m_ref->addItem(QStringLiteral("Référence : auto (live puis enreg.)"), 0);
	m_ref->addItem(QStringLiteral("Référence : début du live"), 1);
	m_ref->addItem(QStringLiteral("Référence : début de l'enregistrement"), 2);
	m_ref->setCurrentIndex((int)m_store->cfg.ref);
	g->addWidget(m_ref, 1, 0, 1, 2);

	m_rewind = new QSpinBox(box);
	m_rewind->setRange(0, 600);
	m_rewind->setSuffix(QStringLiteral(" s"));
	m_rewind->setPrefix(QStringLiteral("Reculer de "));
	m_rewind->setValue(m_store->cfg.rewindSec);
	m_rewind->setToolTip(QStringLiteral("Place le marqueur N secondes AVANT l'appui (utile quand on réagit après coup)."));
	g->addWidget(m_rewind, 1, 2);

	m_dir = new QLineEdit(m_store->cfg.outDir, box);
	auto *browse = new QPushButton(QStringLiteral("…"), box);
	browse->setMaximumWidth(32);
	auto *open = new QPushButton(QStringLiteral("Ouvrir le dossier"), box);
	g->addWidget(m_dir, 2, 0, 1, 2);
	g->addWidget(browse, 2, 2, Qt::AlignLeft);
	g->addWidget(open, 3, 0, 1, 1);
	m_file = new QLabel(box);
	m_file->setTextInteractionFlags(Qt::TextSelectableByMouse);
	g->addWidget(m_file, 3, 1, 1, 2);
	root->addWidget(box);

	auto *credit = new QLabel(QStringLiteral("<small>bart_2216 Studio · v1.0.0</small>"), this);
	credit->setAlignment(Qt::AlignRight);
	root->addWidget(credit);

	// ---- connexions
	connect(m_markBtn, &QPushButton::clicked, this, &MarkerDock::onMarkClicked);
	connect(m_title, &QLineEdit::returnPressed, this, &MarkerDock::onMarkClicked);
	connect(del, &QPushButton::clicked, this, &MarkerDock::onDelete);
	connect(newS, &QPushButton::clicked, this, &MarkerDock::onNewSession);
	connect(browse, &QPushButton::clicked, this, &MarkerDock::onBrowse);
	connect(open, &QPushButton::clicked, this, &MarkerDock::onOpenFolder);

	connect(m_clip, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
	connect(m_csv, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
	connect(m_txt, &QCheckBox::toggled, this, [this](bool) { applyOptions(); });
	connect(m_sep, &QComboBox::currentIndexChanged, this, [this](int) { applyOptions(); });
	connect(m_ref, &QComboBox::currentIndexChanged, this, [this](int) { applyOptions(); });
	connect(m_rewind, &QSpinBox::valueChanged, this, [this](int) { applyOptions(); });
	connect(m_dir, &QLineEdit::editingFinished, this, [this]() { applyOptions(); });

	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
		if (m_building || it->column() != 4)
			return;
		m_store->setTitle(it->row(), it->text());
	});

	m_timer = new QTimer(this);
	m_timer->setInterval(500);
	connect(m_timer, &QTimer::timeout, this, &MarkerDock::onTick);
	m_timer->start();

	refreshTable();
	onTick();
}

// ------------------------------------------------------------ utilitaires
void MarkerDock::flash(const QString &text)
{
	m_note->setText(text);
	QTimer::singleShot(5000, this, [this]() { m_note->clear(); });
}

void MarkerDock::applyOptions()
{
	if (m_building)
		return;
	auto &c = m_store->cfg;
	c.saveClip = m_clip->isChecked();
	c.writeCsv = m_csv->isChecked();
	c.writeTxt = m_txt->isChecked();
	c.csvSep = m_sep->currentData().toString() == QStringLiteral(",") ? QLatin1Char(',') : QLatin1Char(';');
	const int r = m_ref->currentIndex();
	c.ref = r == 1 ? RefMode::Stream : r == 2 ? RefMode::Record : RefMode::Auto;
	c.rewindSec = m_rewind->value();
	const QString d = m_dir->text().trimmed();
	if (!d.isEmpty())
		c.outDir = d;
	m_store->write();
	m_store->saveConfig();
	refreshTable();
}

void MarkerDock::refreshTable()
{
	m_building = true;
	const auto &v = m_store->markers;
	m_table->setRowCount((int)v.size());
	auto mk = [](const QString &t, bool editable) {
		auto *it = new QTableWidgetItem(t);
		Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
		if (editable)
			f |= Qt::ItemIsEditable;
		it->setFlags(f);
		return it;
	};
	for (int r = 0; r < (int)v.size(); r++) {
		const Marker &m = v[r];
		const QString live = MarkerStore::fmtTc(m.streamMs, false);
		const QString rec = MarkerStore::fmtTc(m.recMs, false);
		m_table->setItem(r, 0, mk(QString::number(m.index), false));
		m_table->setItem(r, 1, mk(m.wall.toString(QStringLiteral("HH:mm:ss")), false));
		m_table->setItem(r, 2, mk(live.isEmpty() ? QStringLiteral("—") : live, false));
		m_table->setItem(r, 3, mk(rec.isEmpty() ? QStringLiteral("—") : rec, false));
		m_table->setItem(r, 4, mk(m.title, true));
		m_table->setItem(r, 5, mk(m.clip.isEmpty() ? QStringLiteral("—") : QFileInfo(m.clip).fileName(), false));
	}
	m_table->scrollToBottom();
	m_building = false;

	if (m_store->hasSession()) {
		const QString p = m_store->cfg.writeCsv ? m_store->csvPath() : m_store->txtPath();
		m_file->setText(QStringLiteral("<small>Fichier : %1</small>").arg(QFileInfo(p).fileName().toHtmlEscaped()));
	} else {
		m_file->setText(QStringLiteral("<small>Aucune session (créée au 1er marqueur)</small>"));
	}
}

// ------------------------------------------------------------- marqueurs
void MarkerDock::addMarker(const QString &title, quint64 nowNs)
{
	const int id = m_store->add(title, nowNs);

	if (m_store->cfg.saveClip) {
		if (obs_frontend_replay_buffer_active()) {
			m_pendingClips.push_back(id);
			obs_frontend_replay_buffer_save();
		} else {
			flash(QStringLiteral("Replay buffer inactif : marqueur ajouté, mais aucun clip sauvegardé."));
		}
	}
	if (!m_store->streaming() && !m_store->recording())
		flash(QStringLiteral("Aucun live ni enregistrement en cours : pas de timecode (heure locale seulement)."));

	refreshTable();
}

void MarkerDock::onMarkClicked()
{
	const quint64 ts = os_gettime_ns();
	addMarker(m_title->text(), ts);
	m_title->clear();
}

void MarkerDock::hotkeyMarker(int preset, quint64 nowNs)
{
	if (preset >= 0 && preset < m_store->cfg.presets.size()) {
		addMarker(m_store->cfg.presets[preset], nowNs);
	} else {
		addMarker(m_title->text(), nowNs);
		m_title->clear();
	}
}

void MarkerDock::onDelete()
{
	const int r = m_table->currentRow();
	if (r < 0)
		return;
	m_store->removeAt(r);
	refreshTable();
}

void MarkerDock::onNewSession()
{
	m_store->newSession();
	m_pendingClips.clear();
	refreshTable();
}

void MarkerDock::onBrowse()
{
	const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier d'export"), m_dir->text());
	if (!d.isEmpty()) {
		m_dir->setText(QDir::toNativeSeparators(d));
		applyOptions();
	}
}

void MarkerDock::onOpenFolder()
{
	QDir().mkpath(m_store->cfg.outDir);
	QDesktopServices::openUrl(QUrl::fromLocalFile(m_store->cfg.outDir));
}

// --------------------------------------------------------- evenements OBS
void MarkerDock::onStreamStarted(quint64 ns)
{
	if (!m_store->streaming() && !m_store->recording())
		m_store->newSession();
	m_store->streamStarted(ns);
	refreshTable();
}

void MarkerDock::onStreamStopped()
{
	m_store->streamStopped();
}

void MarkerDock::onRecordStarted(quint64 ns)
{
	if (!m_store->streaming() && !m_store->recording())
		m_store->newSession();
	m_store->recordStarted(ns);
	refreshTable();
}

void MarkerDock::onRecordStopped()
{
	m_store->recordStopped();
}

void MarkerDock::onReplaySaved()
{
	char *p = obs_frontend_get_last_replay();
	if (!p)
		return;
	const QString path = QString::fromUtf8(p);
	bfree(p);
	if (m_pendingClips.empty())
		return;
	const int id = m_pendingClips.front();
	m_pendingClips.pop_front();
	m_store->setClip(id, QDir::toNativeSeparators(path));
	refreshTable();
}

void MarkerDock::onReplayStopped()
{
	m_pendingClips.clear();
}

void MarkerDock::shutdown()
{
	if (m_timer)
		m_timer->stop();
	m_store->saveConfig();
}

void MarkerDock::onTick()
{
	auto dot = [](bool on) {
		return on ? QStringLiteral("<span style='color:#2ecc40'>●</span>")
			  : QStringLiteral("<span style='color:#777'>○</span>");
	};
	m_status->setText(QStringLiteral("Live %1   Enreg. %2   Replay buffer %3   —   %4 marqueur(s)")
				  .arg(dot(obs_frontend_streaming_active()),
				       dot(obs_frontend_recording_active()),
				       dot(obs_frontend_replay_buffer_active()))
				  .arg((int)m_store->markers.size()));
}
