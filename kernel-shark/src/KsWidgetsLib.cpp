// SPDX-License-Identifier: LGPL-2.1

/*
 * Copyright (C) 2017 VMware Inc, Yordan Karadzhov <ykaradzhov@vmware.com>
 */

/**
 *  @file    KsWidgetsLib.cpp
 *  @brief   Defines small widgets and dialogues used by the KernelShark GUI.
 */

// KernelShark
#include "KsCmakeDef.hpp"
#include "KsPlotTools.hpp"
#include "KsWidgetsLib.hpp"

namespace KsWidgetsLib
{

/**
 * @brief Create KsProgressBar.
 *
 * @param message: Text to be shown.
 * @param parent: The parent of this widget.
 */
KsProgressBar::KsProgressBar(QString message, QWidget *parent)
: QWidget(parent),
  _sb(this),
  _pb(&_sb) {
	resize(KS_BROGBAR_WIDTH, KS_BROGBAR_HEIGHT);
	setWindowTitle("KernelShark");
	setLayout(new QVBoxLayout);

	_pb.setOrientation(Qt::Horizontal);
	_pb.setTextVisible(false);
	_pb.setRange(0, KS_PROGRESS_BAR_MAX);
	_pb.setValue(1);

	_sb.addPermanentWidget(&_pb, 1);

	layout()->addWidget(new QLabel(message));
	layout()->addWidget(&_sb);

	setWindowFlags(Qt::WindowStaysOnTopHint);

	show();
}

/** @brief Set the state of the progressbar.
 *
 * @param i: A value ranging from 0 to KS_PROGRESS_BAR_MAX.
 */
void KsProgressBar::setValue(int i) {
	_pb.setValue(i);
	QApplication::processEvents();
}

/**
 * @brief Create KsMessageDialog.
 *
 * @param message: Text to be shown.
 * @param parent: The parent of this widget.
 */
KsMessageDialog::KsMessageDialog(QString message, QWidget *parent)
: QDialog(parent),
  _text(message, this),
  _closeButton("Close", this)
{
	resize(KS_MSG_DIALOG_WIDTH, KS_MSG_DIALOG_HEIGHT);

	_layout.addWidget(&_text);
	_layout.addWidget(&_closeButton);

	connect(&_closeButton,	&QPushButton::pressed,
		this,		&QWidget::close);

	this->setLayout(&_layout);
}

/**
 * @brief Launch a File exists dialog. Use this function to ask the user
 * before overwriting an existing file.
 *
 * @param fileName: the name of the file.
 *
 * @returns True if the user wants to overwrite the file. Otherwise
 */
bool fileExistsDialog(QString fileName)
{
	QString msg("A file ");
	QMessageBox msgBox;

	msg += fileName;
	msg += " already exists.";
	msgBox.setText(msg);
	msgBox.setInformativeText("Do you want to replace it?");

	msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Cancel);
	msgBox.setDefaultButton(QMessageBox::Cancel);

	return (msgBox.exec() == QMessageBox::Save);
}

}; // KsWidgetsLib

/**
 * @brief Create KsCheckBoxWidget.
 *
 * @param name: The name of this widget.
 * @param parent: The parent of this widget.
 */
KsCheckBoxWidget::KsCheckBoxWidget(int sd, const QString &name,
				   QWidget *parent)
: QWidget(parent),
  _tb(this),
  _sd(sd),
  _allCb("all", &_tb),
  _allCbAction(nullptr),
  _cbWidget(this),
  _cbLayout(&_cbWidget),
  _topLayout(this),
  _stramLabel("", this),
  _name(name),
  _nameLabel(name + ":  ", &_tb)
{
	setWindowTitle(_name);
	setMinimumHeight(SCREEN_HEIGHT / 2);

	connect(&_allCb,	&QCheckBox::clicked,
		this,		&KsCheckBoxWidget::_checkAll);

	_cbWidget.setLayout(&_cbLayout);

	_topLayout.addWidget(&_stramLabel);

	_tb.addWidget(&_nameLabel);
	_allCbAction = _tb.addWidget(&_allCb);

	_topLayout.addWidget(&_tb);

	_topLayout.addWidget(&_cbWidget);
	_topLayout.setContentsMargins(0, 0, 0, 0);

	setLayout(&_topLayout);
	_allCb.setCheckState(Qt::Checked);
// 	setVisibleCbAll(false);
}

/**
 * Set the default state for all checkboxes (including the "all" checkbox).
 */
void KsCheckBoxWidget::setDefault(bool st)
{
	Qt::CheckState state = Qt::Unchecked;

	if (st)
		state = Qt::Checked;

	_allCb.setCheckState(state);
	_checkAll(state);
}

/** Get a vector containing the indexes of all checked boxes. */
QVector<int> KsCheckBoxWidget::getCheckedIds()
{
	QVector<int> vec;
	int n = _id.size();

	for (int i = 0; i < n; ++i)
		if (_checkState(i) == Qt::Checked)
			vec.append(_id[i]);

	return vec;
}

/**
 * @brief Set the state of the checkboxes.
 *
 * @param v: Vector containing the bool values for all checkboxes.
 */
void KsCheckBoxWidget::set(QVector<bool> v)
{
	Qt::CheckState state;
	int nChecks;

	nChecks = (v.size() < _id.size()) ? v.size() : _id.size();

	/* Start with the "all" checkbox being checked. */
	_allCb.setCheckState(Qt::Checked);
	for (int i = 0; i < nChecks; ++i) {
		if (v[i]) {
			state = Qt::Checked;
		} else {
			/*
			 * At least one checkbox is unchecked. Uncheck
			 * "all" as well.
			 */
			state = Qt::Unchecked;
			_allCb.setCheckState(state);
		}

		_setCheckState(i, state);
	}
	_verify();
}

void KsCheckBoxWidget::_checkAll(bool st)
{
	Qt::CheckState state = Qt::Unchecked;
	int n = _id.size();

	if (st) state = Qt::Checked;

	for (int i = 0; i < n; ++i) {
		_setCheckState(i, state);
	}

	_verify();
}

/**
 * @brief Create KsCheckBoxDialog.
 *
 * @param cbw: A KsCheckBoxWidget to be nested in this dialog.
 * @param parent: The parent of this widget.
 */
KsCheckBoxDialog::KsCheckBoxDialog(QVector<KsCheckBoxWidget *> cbws, QWidget *parent)
: QDialog(parent), _checkBoxWidgets(cbws),
  _applyButton("Apply", this),
  _cancelButton("Cancel", this)
{
	int buttonWidth;

	if (!cbws.isEmpty())
		setWindowTitle(cbws[0]->name());

	for (auto const &w: _checkBoxWidgets)
		_cbLayout.addWidget(w);
	_topLayout.addLayout(&_cbLayout);

	buttonWidth = STRING_WIDTH("--Cancel--");
	_applyButton.setFixedWidth(buttonWidth);
	_cancelButton.setFixedWidth(buttonWidth);

	_buttonLayout.addWidget(&_applyButton);
	_applyButton.setAutoDefault(false);

	_buttonLayout.addWidget(&_cancelButton);
	_cancelButton.setAutoDefault(false);

	_buttonLayout.setAlignment(Qt::AlignLeft);
	_topLayout.addLayout(&_buttonLayout);

	_applyButtonConnection =
		connect(&_applyButton,	&QPushButton::pressed,
			this,		&KsCheckBoxDialog::_applyPress);

	connect(&_applyButton,	&QPushButton::pressed,
		this,		&QWidget::close);

	connect(&_cancelButton,	&QPushButton::pressed,
		this,		&QWidget::close);

	this->setLayout(&_topLayout);
}

void KsCheckBoxDialog::_applyPress()
{
	QVector<int> vec;

	/*
	 * Disconnect _applyButton. This is done in order to protect
	 * against multiple clicks.
	 */
	disconnect(_applyButtonConnection);

	for (auto const &w: _checkBoxWidgets) {
		vec = w->getCheckedIds();
		emit apply(w->sd(), vec);
	}
}

KsComboPlotDialog::KsComboPlotDialog(QWidget *parent)
: _hostStreamLabel("Host data stream:"),
  _guestStreamLabel("Guest data stream"),
  _hostStreamComboBox(this),
  _guestStreamComboBox(this),
  _vcpuCheckBoxWidget(nullptr),
  _hostCheckBoxWidget(nullptr),
  _applyButton("Apply", this),
  _cancelButton("Cancel", this)
{
	kshark_context *kshark_ctx(nullptr);
	int *streamIds, buttonWidth;
	int sdHost(0), sdGuest(1);
	QStringList streamList;

	auto lamAddLine = [&] {
		QFrame* line = new QFrame();

		line->setFrameShape(QFrame::HLine);
		line->setFrameShadow(QFrame::Sunken);
		_topLayout.addWidget(line);
	};

	setWindowTitle("Combo Plots");

	if (!kshark_instance(&kshark_ctx) ||
	    kshark_ctx->n_streams < 2)
		return;

	streamIds = kshark_all_streams(kshark_ctx);
	_hostStreamComboBox.addItem(QString::number(streamIds[0]));
	for (int i = 1; i < kshark_ctx->n_streams; ++i) {
		_hostStreamComboBox.addItem(QString::number(streamIds[i]));
		_guestStreamComboBox.addItem(QString::number(streamIds[i]));
	}

	_streamMenuLayout.addWidget(&_hostStreamLabel, 0, 0);
	_streamMenuLayout.addWidget(&_hostStreamComboBox, 0, 1);

	_streamMenuLayout.addWidget(&_guestStreamLabel, 1, 0);
	_streamMenuLayout.addWidget(&_guestStreamComboBox, 1, 1);

	_topLayout.addLayout(&_streamMenuLayout);

	lamAddLine();

	_hostCheckBoxWidget = new KsTasksCheckBoxWidget(sdHost, true, this);
	_hostCheckBoxWidget->setStream(QString(kshark_ctx->stream[sdHost]->file));
	_hostCheckBoxWidget->setSingleSelection();
	_hostCheckBoxWidget->setDefault(false);

	_vcpuCheckBoxWidget = new KsCPUCheckBoxWidget(sdGuest, this);
	_vcpuCheckBoxWidget->setStream(QString(kshark_ctx->stream[sdGuest]->file));
	_vcpuCheckBoxWidget->setSingleSelection();
	_vcpuCheckBoxWidget->setDefault(false);

	_cbLayout.addWidget(_hostCheckBoxWidget);
	_cbLayout.addWidget(_vcpuCheckBoxWidget);

	_topLayout.addLayout(&_cbLayout);

	buttonWidth = STRING_WIDTH("--Cancel--");
	_applyButton.setFixedWidth(buttonWidth);
	_cancelButton.setFixedWidth(buttonWidth);

	_buttonLayout.addWidget(&_applyButton);
	_applyButton.setAutoDefault(false);

	_buttonLayout.addWidget(&_cancelButton);
	_cancelButton.setAutoDefault(false);

	_buttonLayout.setAlignment(Qt::AlignLeft);
	_topLayout.addLayout(&_buttonLayout);

	_applyButtonConnection =
		connect(&_applyButton,	&QPushButton::pressed,
			this,		&KsComboPlotDialog::_applyPress);

	connect(&_applyButton,	&QPushButton::pressed,
		this,		&QWidget::close);

	connect(&_cancelButton,	&QPushButton::pressed,
		this,		&QWidget::close);

	/*
	 * Using the old Signal-Slot syntax because QComboBox::currentIndexChanged
	 * has overloads.
	 */
	connect(&_hostStreamComboBox,	SIGNAL(currentIndexChanged(const QString &)),
		this,			SLOT(_hostStreamChanged(const QString &)));

	connect(&_guestStreamComboBox,	SIGNAL(currentIndexChanged(const QString &)),
		this,			SLOT(_guestStreamChanged(const QString &)));

	setLayout(&_topLayout);
}

void KsComboPlotDialog::_applyPress()
{
	QVector<int> combo(4);

	/*
	 * Disconnect _applyButton. This is done in order to protect
	 * against multiple clicks.
	 */
	disconnect(_applyButtonConnection);

	combo[0] = _hostStreamComboBox.currentText().toInt();
	combo[1] = _hostCheckBoxWidget->getCheckedIds()[0];
	combo[2] = _guestStreamComboBox.currentText().toInt();
	combo[3] = _vcpuCheckBoxWidget->getCheckedIds()[0];

	emit apply(-1, combo);
}

void KsComboPlotDialog::_hostStreamChanged(const QString &sdStr)
{
	qInfo() << "_hostStreamChanged";
	kshark_context *kshark_ctx(nullptr);
	int *streamIds, sdHost;
	QStringList streamList;

	if (!kshark_instance(&kshark_ctx))
		return;

	sdHost = sdStr.toInt();
	_guestStreamComboBox.clear();
	streamIds = kshark_all_streams(kshark_ctx);
	for (int i = 0; i < kshark_ctx->n_streams; ++i)
		if (sdHost != streamIds[i])
			_guestStreamComboBox.addItem(QString::number(streamIds[i]));
}

void KsComboPlotDialog::_guestStreamChanged(const QString &sdStr)
{
	qInfo() << "_guestStreamChanged";
	kshark_context *kshark_ctx(nullptr);
	int sdGuest;

	if (!kshark_instance(&kshark_ctx))
		return;

	sdGuest = sdStr.toInt();

	delete _vcpuCheckBoxWidget;

	_vcpuCheckBoxWidget = new KsCPUCheckBoxWidget(sdGuest, this);
	_vcpuCheckBoxWidget->setDefault(false);
	_cbLayout.addWidget(_vcpuCheckBoxWidget);
}

/**
 * @brief Create KsCheckBoxTable.
 *
 * @param parent: The parent of this widget.
 */
KsCheckBoxTable::KsCheckBoxTable(QWidget *parent)
: QTableWidget(parent)
{
	setShowGrid(false);
	horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
	horizontalHeader()->setStretchLastSection(true);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	verticalHeader()->setVisible(false);

	connect(this, &QTableWidget::cellDoubleClicked,
		this, &KsCheckBoxTable::_doubleClicked);
}

/**
 * @brief Initialize the table.
 *
 * @param headers: The headers of the individual columns.
 * @param size: The number of rows.
 */
void KsCheckBoxTable::init(QStringList headers, int size)
{
	QHBoxLayout *cbLayout;
	QWidget *cbWidget;

	setColumnCount(headers.count());
	setRowCount(size);
	setHorizontalHeaderLabels(headers);

	_cb.resize(size);

	for (int i = 0; i < size; ++i) {
		cbWidget = new QWidget();
		_cb[i] = new QCheckBox(cbWidget);
		cbLayout = new QHBoxLayout(cbWidget);

		cbLayout->addWidget(_cb[i]);
		cbLayout->setAlignment(Qt::AlignCenter);
		cbLayout->setContentsMargins(0, 0, 0, 0);

		cbWidget->setLayout(cbLayout);
		setCellWidget(i, 0, cbWidget);
	}
}

/** Reimplemented event handler used to receive key press events. */
void KsCheckBoxTable::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Return) {
		for (auto &s: selectedItems()) {
			if (s->column() == 1)
				emit changeState(s->row());
		}
	}

	QApplication::processEvents();
	QTableWidget::keyPressEvent(event);
}

/** Reimplemented event handler used to receive mouse press events. */
void KsCheckBoxTable::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		for (auto &i: selectedItems())
			i->setSelected(false);

		return;
	}

	QApplication::processEvents();
	QTableWidget::mousePressEvent(event);
}

void KsCheckBoxTable::_doubleClicked(int row, int col)
{
	emit changeState(row);
	for (auto &i: selectedItems())
		i->setSelected(false);
}

/**
 * @brief Create KsCheckBoxTableWidget.
 *
 * @param name: The name of this widget.
 * @param parent: The parent of this widget.
 */
KsCheckBoxTableWidget::KsCheckBoxTableWidget(int sd, const QString &name,
					     QWidget *parent)
: KsCheckBoxWidget(sd, name, parent),
  _table(this)
{
	connect(&_table,	&KsCheckBoxTable::changeState,
		this,		&KsCheckBoxTableWidget::_changeState);
}

/** Initialize the KsCheckBoxTable and its layout. */
void KsCheckBoxTableWidget::_initTable(QStringList headers, int size)
{
	_table.init(headers, size);

	for (auto const & cb: _table._cb) {
		connect(cb,	&QCheckBox::clicked,
			this,	&KsCheckBoxTableWidget::_update);
	}

	_cbLayout.setContentsMargins(1, 1, 1, 1);
	_cbLayout.addWidget(&_table);
}

/** Adjust the size of this widget according to its content. */
void KsCheckBoxTableWidget::_adjustSize()
{
	int width;

	_table.setVisible(false);
	_table.resizeColumnsToContents();
	_table.setVisible(true);

	width = _table.horizontalHeader()->length() +
		FONT_WIDTH * 3 +
		style()->pixelMetric(QStyle::PM_ScrollBarExtent);

	_cbWidget.resize(width, _cbWidget.height());

	setMinimumWidth(_cbWidget.width() +
			_cbLayout.contentsMargins().left() +
			_cbLayout.contentsMargins().right() +
			_topLayout.contentsMargins().left() +
			_topLayout.contentsMargins().right());
}

void  KsCheckBoxTableWidget::_update(bool state)
{
	/* If a Checkbox is being unchecked. Unchecked "all" as well. */
	if (!state)
		_allCb.setCheckState(Qt::Unchecked);
}

void KsCheckBoxTableWidget::_changeState(int row)
{
	if (_table._cb[row]->checkState() == Qt::Checked)
		_table._cb[row]->setCheckState(Qt::Unchecked);
	else
		_table._cb[row]->setCheckState(Qt::Checked);

	_allCb.setCheckState(Qt::Checked);
	for (auto &c: _table._cb) {
		if (c->checkState() == Qt::Unchecked) {
			_allCb.setCheckState(Qt::Unchecked);
			break;
		}
	}
}

static void update_r(QTreeWidgetItem *item, Qt::CheckState state)
{
	int n;

	item->setCheckState(0, state);

	n = item->childCount();
	for (int i = 0; i < n; ++i)
		update_r(item->child(i), state);
}

/**
 * @brief Create KsCheckBoxTree.
 *
 * @param parent: The parent of this widget.
 */
KsCheckBoxTree::KsCheckBoxTree(QWidget *parent)
: QTreeWidget(parent)
{
	setColumnCount(2);
	setHeaderHidden(true);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	connect(this, &KsCheckBoxTree::itemDoubleClicked,
		this, &KsCheckBoxTree::_doubleClicked);
}

/** Reimplemented event handler used to receive key press events. */
void KsCheckBoxTree::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Return) {
		/* Loop over all selected child items and change
		* there states. */
		for (auto &s: selectedItems()) {
			if(s->childCount()) {
				if (s->isExpanded())
					continue;
			}

			if (s->checkState(0) == Qt::Unchecked)
				s->setCheckState(0, Qt::Checked);
			else
				s->setCheckState(0, Qt::Unchecked);

			if(s->childCount()) {
				update_r(s, s->checkState(0));
			}
		}
	}

	emit verify();
	QTreeWidget::keyPressEvent(event);
}

void KsCheckBoxTree::_doubleClicked(QTreeWidgetItem *item, int col)
{
	if (item->checkState(0) == Qt::Unchecked)
		item->setCheckState(0, Qt::Checked);
	else
		item->setCheckState(0, Qt::Unchecked);

	for (auto &i: selectedItems())
		i->setSelected(false);

	emit itemClicked(item, col);
}

/** Reimplemented event handler used to receive mouse press events. */
void KsCheckBoxTree::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		for (auto &i: selectedItems())
			i->setSelected(false);
		return;
	}

	QApplication::processEvents();
	QTreeWidget::mousePressEvent(event);
}

/**
 * @brief Create KsCheckBoxTreeWidget.
 *
 * @param name: The name of this widget.
 * @param parent: The parent of this widget.
 */
KsCheckBoxTreeWidget::KsCheckBoxTreeWidget(int sd, const QString &name,
					   QWidget *parent)
: KsCheckBoxWidget(sd, name, parent),
  _tree(this)
{
	connect(&_tree,	&KsCheckBoxTree::verify,
		this,	&KsCheckBoxTreeWidget::_verify);
}

/** Initialize the KsCheckBoxTree and its layout. */
void KsCheckBoxTreeWidget::_initTree()
{
	_tree.setSelectionMode(QAbstractItemView::MultiSelection);

	connect(&_tree, &QTreeWidget::itemClicked,
		this,	&KsCheckBoxTreeWidget::_update);

	_cbLayout.setContentsMargins(1, 1, 1, 1);
	_cbLayout.addWidget(&_tree);
}

/** Adjust the size of this widget according to its content. */
void KsCheckBoxTreeWidget::_adjustSize()
{
	int width, n = _tree.topLevelItemCount();

	if (n == 0)
		return;

	for (int i = 0; i < n; ++i)
		_tree.topLevelItem(i)->setExpanded(true);

	_tree.resizeColumnToContents(0);
	if (_tree.topLevelItem(0)->child(0)) {
		width = _tree.visualItemRect(_tree.topLevelItem(0)->child(0)).width();
	} else {
		width = _tree.visualItemRect(_tree.topLevelItem(0)).width();
	}

	width += FONT_WIDTH*3 + style()->pixelMetric(QStyle::PM_ScrollBarExtent);
	_cbWidget.resize(width, _cbWidget.height());

	for (int i = 0; i < n; ++i)
		_tree.topLevelItem(i)->setExpanded(false);

	setMinimumWidth(_cbWidget.width() +
			_cbLayout.contentsMargins().left() +
			_cbLayout.contentsMargins().right() +
			_topLayout.contentsMargins().left() +
			_topLayout.contentsMargins().right());
}

void KsCheckBoxTreeWidget::_update(QTreeWidgetItem *item, int column)
{
	/* Get the new state of the item. */
	Qt::CheckState state = item->checkState(0);

	/* Recursively update all items below this one. */
	update_r(item, state);

	/*
	 * Update all items above this one including the "all"
	 * check box.
	 */
	_verify();
}

void KsCheckBoxTreeWidget::_verify()
{
	/*
	 * Set the state of the top level items according to the
	 * state of the childs.
	 */
	QTreeWidgetItem *topItem, *childItem;

	for(int t = 0; t < _tree.topLevelItemCount(); ++t) {
		topItem = _tree.topLevelItem(t);
		if (topItem->childCount() == 0)
			continue;

		topItem->setCheckState(0, Qt::Checked);
		for (int c = 0; c < topItem->childCount(); ++c) {
			childItem = topItem->child(c);
			if (childItem->checkState(0) == Qt::Unchecked)
				topItem->setCheckState(0, Qt::Unchecked);
		}
	}

	_allCb.setCheckState(Qt::Checked);
	for (auto &c: _cb) {
		if (c->checkState(0) == Qt::Unchecked) {
			_allCb.setCheckState(Qt::Unchecked);
			break;
		}
	}
}

/**
 * @brief Create KsCPUCheckBoxWidget.
 *
 * @param sd: Data stream identifier.
 * @param parent: The parent of this widget.
 */
KsCPUCheckBoxWidget::KsCPUCheckBoxWidget(int sd, QWidget *parent)
: KsCheckBoxTreeWidget(sd, "CPUs", parent)
{
	int nCPUs(0), height(FONT_HEIGHT * 1.5);
	kshark_context *kshark_ctx(nullptr);
	kshark_data_stream *stream;
	KsPlot::ColorTable colors;
	QString style;

	if (!kshark_instance(&kshark_ctx))
		return;

	style = QString("QTreeView::item { height: %1 ;}").arg(height);
	_tree.setStyleSheet(style);

	_initTree();

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (stream)
		nCPUs = tep_get_cpus(stream->pevent);

	_id.resize(nCPUs);
	_cb.resize(nCPUs);
	colors = KsPlot::getCPUColorTable();

	for (int i = 0; i < nCPUs; ++i) {
		QTreeWidgetItem *cpuItem = new QTreeWidgetItem;
		cpuItem->setText(0, "  ");
		cpuItem->setText(1, QString("CPU %1").arg(i));
		cpuItem->setCheckState(0, Qt::Checked);
		cpuItem->setBackgroundColor(0, QColor(colors[i].r(),
						      colors[i].g(),
						      colors[i].b()));
		_tree.addTopLevelItem(cpuItem);
		_id[i] = i;
		_cb[i] = cpuItem;
	}

	_adjustSize();
}

/**
 * @brief Create KsEventsCheckBoxWidget.
 *
 * @param sd: Data stream identifier.
 * @param parent: The parent of this widget.
 */
// KsEventsCheckBoxWidget::KsEventsCheckBoxWidget(int sd, QWidget *parent)
// : KsCheckBoxTreeWidget(sd, "Events", parent)
// {
// 	QTreeWidgetItem *sysItem, *evtItem;
// 	tep_event_format **events(nullptr);
// 	kshark_context *kshark_ctx(nullptr);
// 	kshark_data_stream *stream;
// 	QString sysName, evtName;
// 	int nEvts(0), i(0);
// 
// 	if (!kshark_instance(&kshark_ctx))
// 		return;
// 
// 	stream = kshark_get_data_stream(kshark_ctx, sd);
// 	if (stream) {
// 		nEvts = tep_get_events_count(stream->pevent);
// 		events = tep_list_events(stream->pevent, TEP_EVENT_SORT_SYSTEM);
// 	}
// 
// 	_initTree();
// 	_id.resize(nEvts);
// 	_cb.resize(nEvts);
// 
// 	while (i < nEvts) {
// 		sysName = events[i]->system;
// 		sysItem = new QTreeWidgetItem;
// 		sysItem->setText(0, sysName);
// 		sysItem->setCheckState(0, Qt::Checked);
// 		_tree.addTopLevelItem(sysItem);
// 
// 		while (sysName == events[i]->system) {
// 			evtName = events[i]->name;
// 			evtItem = new QTreeWidgetItem;
// 			evtItem->setText(0, evtName);
// 			evtItem->setCheckState(0, Qt::Checked);
// 			evtItem->setFlags(evtItem->flags() |
// 					  Qt::ItemIsUserCheckable);
// 
// 			sysItem->addChild(evtItem);
// 
// 			_id[i] = events[i]->id;
// 			_cb[i] = evtItem;
// 
// 			if (++i == nEvts)
// 				break;
// 		}
// 	}
// 
// 	_tree.sortItems(0, Qt::AscendingOrder);
// 	_adjustSize();
// }

KsEventsCheckBoxWidget::KsEventsCheckBoxWidget(tep_handle *pevent,
					       QWidget *parent)
: KsCheckBoxTreeWidget(-1, "Events", parent)
{
	tep_event **events;
	int nEvts;

	if (pevent) {
		nEvts = tep_get_events_count(pevent);
		events = tep_list_events(pevent,
					TEP_EVENT_SORT_SYSTEM);
		_makeItems(events, nEvts);
	}
}

/**
 * @brief Create KsEventsCheckBoxWidget.
 *
 * @param pevent: Page event used to parse the page.
 * @param parent: The parent of this widget.
 */
KsEventsCheckBoxWidget::KsEventsCheckBoxWidget(int sd, QWidget *parent)
: KsCheckBoxTreeWidget(sd, "Events", parent)
{
	kshark_context *kshark_ctx(nullptr);
	kshark_data_stream *stream;
	tep_event **events;
	int nEvts(0);

	if (!kshark_instance(&kshark_ctx))
		return;

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (stream) {
		nEvts = tep_get_events_count(stream->pevent);
		events = tep_list_events(stream->pevent,
					 TEP_EVENT_SORT_SYSTEM);
		_makeItems(events, nEvts);
	}
}

void KsEventsCheckBoxWidget::_makeItems(tep_event **events, int nEvts)
{
	QTreeWidgetItem *sysItem, *evtItem;
	QString sysName, evtName;
	int i(0);

	_initTree();
	_id.resize(nEvts);
	_cb.resize(nEvts);

	while (i < nEvts) {
		sysName = events[i]->system;
		sysItem = new QTreeWidgetItem;
		sysItem->setText(0, sysName);
		sysItem->setCheckState(0, Qt::Checked);
		_tree.addTopLevelItem(sysItem);

		while (sysName == events[i]->system) {
			evtName = events[i]->name;
			evtItem = new QTreeWidgetItem;
			evtItem->setText(0, evtName);
			evtItem->setCheckState(0, Qt::Checked);
			evtItem->setFlags(evtItem->flags() |
					  Qt::ItemIsUserCheckable);

			sysItem->addChild(evtItem);

			_id[i] = events[i]->id;
			_cb[i] = evtItem;

			if (++i == nEvts)
				break;
		}
	}

	_tree.sortItems(0, Qt::AscendingOrder);
	_adjustSize();
}

/** Remove a System from the Checkbox tree. */
void KsEventsCheckBoxWidget::removeSystem(QString name) {
	QTreeWidgetItem *item =
		_tree.findItems(name, Qt::MatchFixedString, 0)[0];

	int index = _tree.indexOfTopLevelItem(item);
	if (index >= 0)
		_tree.takeTopLevelItem(index);
}

/**
 * @brief Create KsTasksCheckBoxWidget.
 *
 * @param sd: Data stream identifier.
 * @param cond: If True make a "Show Task" widget. Otherwise make "Hide Task".
 * @param parent: The parent of this widget.
 */
KsTasksCheckBoxWidget::KsTasksCheckBoxWidget(int sd, bool cond, QWidget *parent)
: KsCheckBoxTableWidget(sd, "Tasks", parent),
  _cond(cond)
{
	kshark_context *kshark_ctx(nullptr);
	QTableWidgetItem *pidItem, *comItem;
	kshark_data_stream *stream;
	KsPlot::ColorTable colors;
	QStringList headers;
	const char *comm;
	int nTasks, pid;

	if (!kshark_instance(&kshark_ctx))
		return;

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (!stream)
		return;

	if (_cond)
		headers << "Show" << "Pid" << "Task";
	else
		headers << "Hide" << "Pid" << "Task";

	_id = KsUtils::getPidList(sd);
	nTasks = _id.count();
	_initTable(headers, nTasks);
	colors = KsPlot::getTaskColorTable();

	for (int i = 0; i < nTasks; ++i) {
		pid = _id[i];
		pidItem	= new QTableWidgetItem(tr("%1").arg(pid));
		_table.setItem(i, 1, pidItem);

		comm = tep_data_comm_from_pid(stream->pevent, pid);
		comItem = new QTableWidgetItem(tr(comm));

		pidItem->setBackgroundColor(QColor(colors[pid].r(),
						   colors[pid].g(),
						   colors[pid].b()));

		if (_id[i] == 0)
			pidItem->setTextColor(Qt::white);

		_table.setItem(i, 2, comItem);
	}

	_adjustSize();
}

/**
 * @brief Create KsPluginCheckBoxWidget.
 *
 * @param pluginList: A list of plugin names.
 * @param parent: The parent of this widget.
 */
KsPluginCheckBoxWidget::KsPluginCheckBoxWidget(int sd, QStringList pluginList,
					       QWidget *parent)
: KsCheckBoxTableWidget(sd, "Plugins", parent)
{
	QTableWidgetItem *nameItem, *infoItem;
	QStringList headers;
	int nPlgins;

	headers << "Load" << "Name" << "Info";

	nPlgins = pluginList.count();
	_initTable(headers, nPlgins);
	_id.resize(nPlgins);

	for (int i = 0; i < nPlgins; ++i) {
		nameItem = new QTableWidgetItem(pluginList[i]);
		_table.setItem(i, 1, nameItem);
		infoItem = new QTableWidgetItem(" -- ");
		_table.setItem(i, 2, infoItem);
		_id[i] = i;
	}

	_adjustSize();
}
