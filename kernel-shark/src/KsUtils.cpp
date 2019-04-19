// SPDX-License-Identifier: LGPL-2.1

/*
 * Copyright (C) 2017 VMware Inc, Yordan Karadzhov <ykaradzhov@vmware.com>
 */

/**
 *  @file    KsUtils.cpp
 *  @brief   KernelShark Utils.
 */

// KernelShark
#include "KsUtils.hpp"

namespace KsUtils {

/**
 * @brief Get a sorteg vector of Task's Pids.
 *
 * @param sd: Data stream identifier.
 */
QVector<int> getPidList(int sd)
{
	kshark_context *kshark_ctx(nullptr);
	int nTasks, *tempPids;
	QVector<int> pids;

	if (!kshark_instance(&kshark_ctx))
		return pids;

	nTasks = kshark_get_task_pids(kshark_ctx, sd, &tempPids);
	for (int r = 0; r < nTasks; ++r) {
		pids.append(tempPids[r]);
	}

	free(tempPids);

	qSort(pids);

	return pids;
}

/** @brief Get a sorted vector of Id values of a filter. */
QVector<int> getFilterIds(tracecmd_filter_id *filter)
{
	kshark_context *kshark_ctx(nullptr);
	int *cpuFilter, n;
	QVector<int> v;

	if (!kshark_instance(&kshark_ctx))
		return v;

	cpuFilter = tracecmd_filter_ids(filter);
	n = filter->count;
	for (int i = 0; i < n; ++i)
		v.append(cpuFilter[i]);

	qSort(v);

	free(cpuFilter);
	return v;
}

/**
 * Set the bit of the filter mask of the kshark session context responsible
 * for the visibility of the events in the Table View.
 */
void listFilterSync(bool state)
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	if (state) {
		kshark_ctx->filter_mask |= KS_TEXT_VIEW_FILTER_MASK;
	} else {
		kshark_ctx->filter_mask &= ~KS_TEXT_VIEW_FILTER_MASK;
	}
}

/**
 * Set the bit of the filter mask of the kshark session context responsible
 * for the visibility of the events in the Graph View.
 */
void graphFilterSync(bool state)
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	if (state) {
		kshark_ctx->filter_mask |= KS_GRAPH_VIEW_FILTER_MASK;
		kshark_ctx->filter_mask |= KS_EVENT_VIEW_FILTER_MASK;
	} else {
		kshark_ctx->filter_mask &= ~KS_GRAPH_VIEW_FILTER_MASK;
		kshark_ctx->filter_mask &= ~KS_EVENT_VIEW_FILTER_MASK;
	}
}


/**
 * @brief Add a checkbox to a menu.
 *
 * @param menu: Input location for the menu object, to which the checkbox will be added.
 * @param name: The name of the checkbox.
 *
 * @returns The checkbox object;
 */
QCheckBox *addCheckBoxToMenu(QMenu *menu, QString name)
{
	QWidget  *containerWidget = new QWidget(menu);
	containerWidget->setLayout(new QHBoxLayout());
	containerWidget->layout()->setContentsMargins(FONT_WIDTH, FONT_HEIGHT/5,
						      FONT_WIDTH, FONT_HEIGHT/5);
	QCheckBox *checkBox = new QCheckBox(name, menu);
	containerWidget->layout()->addWidget(checkBox);

	QWidgetAction *action = new QWidgetAction(menu);
	action->setDefaultWidget(containerWidget);
	menu->addAction(action);

	return checkBox;
}

/**
 * @brief Simple CPU matching function to be user for data collections.
 *
 * @param kshark_ctx: Input location for the session context pointer.
 * @param e: kshark_entry to be checked.
 * @param sd: Data stream identifier.
 * @param cpu: Matching condition value.
 *
 * @returns True if the CPU of the entry matches the value of "cpu" and
 * 	    the entry is visibility in Graph. Otherwise false.
 */
bool matchCPUVisible(struct kshark_context *kshark_ctx,
		     struct kshark_entry *e, int sd, int *cpu)
{
	return (e->cpu == *cpu &&
		e->stream_id == sd &&
		(e->visible & KS_GRAPH_VIEW_FILTER_MASK));
}

void setElidedText(QLabel* label, QString text,
		   enum Qt::TextElideMode mode,
		   int labelWidth)
{
	QFontMetrics metrix(label->font());
	QString elidedText;
	int textWidth;

	textWidth = labelWidth - FONT_WIDTH * 3;
	elidedText = metrix.elidedText(text, Qt::ElideRight, textWidth);

	while(labelWidth < STRING_WIDTH(elidedText) + FONT_WIDTH * 5) {
		textWidth -= FONT_WIDTH * 3;
		elidedText = metrix.elidedText(text, mode, textWidth);
	}

	label->setText(elidedText);
}

}; // KsUtils

/** A stream operator for converting QColor into KsPlot::Color. */
KsPlot::Color& operator <<(KsPlot::Color &thisColor, const QColor &c)
{
	thisColor.set(c.red(), c.green(), c.blue());

	return thisColor;
}

/** Create a default (empty) KsDataStore. */
KsDataStore::KsDataStore(QWidget *parent)
: QObject(parent),
  _rows(nullptr),
  _dataSize(0)
{}

/** Destroy the KsDataStore object. */
KsDataStore::~KsDataStore()
{}

int KsDataStore::_openDataFile(kshark_context *kshark_ctx,
				const QString &file)
{
	int sd;

	sd = kshark_open(kshark_ctx, file.toStdString().c_str());
	if (sd < 0) {
		qCritical() << "ERROR" << sd << "while loading file " << file;
		return sd;
	}

	kshark_handle_all_plugins(kshark_ctx, sd, KSHARK_PLUGIN_UPDATE);

	return sd;
}

/** Load trace data for file. */
int  KsDataStore::loadDataFile(const QString &file)
{
	kshark_context *kshark_ctx(nullptr);
	ssize_t n;
	int sd;

	if (!kshark_instance(&kshark_ctx))
		return -EFAULT;

	clear();
	_unregisterCPUCollections();

	sd = _openDataFile(kshark_ctx, file);
	n = kshark_load_data_entries(kshark_ctx, sd, &_rows);
	if (n < 0) {
		kshark_close(kshark_ctx, sd);
		return n;
	}

	_dataSize = n;
	registerCPUCollections();

	return sd;
}

/**
 * @brief Append a trace data file to the data-set that is already loaded.
 *	  The clock of the new data will be calibrated in order to be
 *	  compatible with the clock of the prior data.
 *
 * @param file: Trace data file, to be append to the already loaded data.
 * @param calib: Callback function providing the calibration of the clock of
 *		 the associated data.
 * @param argv: Array of arguments for the calibration function.
 */
int KsDataStore::appendDataFile(const QString &file, int64_t shift)
{
	kshark_context *kshark_ctx(nullptr);
	struct kshark_entry **apndRows = NULL;
	struct kshark_entry **mergedRows;
	ssize_t nApnd = 0;
	int sd;

	if (!kshark_instance(&kshark_ctx))
		return -EFAULT;

	_unregisterCPUCollections();

	sd = _openDataFile(kshark_ctx, file);

	kshark_ctx->stream[sd]->calib = kshark_offset_calib;
	kshark_ctx->stream[sd]->calib_array = new int64_t;
	*(kshark_ctx->stream[sd]->calib_array) = shift;
	kshark_ctx->stream[sd]->calib_array_size = 1;

	nApnd = kshark_load_data_entries(kshark_ctx, sd, &apndRows);
	if (nApnd < 0) {
		kshark_close(kshark_ctx, sd);
		return nApnd;
	}

	mergedRows = kshark_data_merge(_rows, _dataSize,
				       apndRows, nApnd);

	free(_rows);
	free(apndRows);

	_dataSize += nApnd;
	_rows = mergedRows;

	registerCPUCollections();

	return sd;
}

void KsDataStore::_freeData()
{
	if (_dataSize > 0) {
		for (size_t r = 0; r < _dataSize; ++r)
			free(_rows[r]);

		free(_rows);
		_rows = nullptr;
	}

	_dataSize = 0;
}

/** Reload the trace data. */
void KsDataStore::reload()
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	_freeData();

	if (kshark_ctx->n_streams == 0)
		return;

	_unregisterCPUCollections();

	_dataSize = kshark_load_all_data_entries(kshark_ctx, &_rows);

	registerCPUCollections();

	emit updateWidgets(this);
}

/** Free the loaded trace data and close the file. */
void KsDataStore::clear()
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	_freeData();
	kshark_close_all(kshark_ctx);
}

/** Get the trace event parser for a given data stream. */
tep_handle *KsDataStore::tep(int sd) const {
	kshark_context *kshark_ctx(nullptr);
	kshark_data_stream *stream;

	if (!kshark_instance(&kshark_ctx))
		return nullptr;

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (stream)
		return stream->pevent;

	return nullptr;
}

/** Update the visibility of the entries (filter). */
void KsDataStore::update()
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	_unregisterCPUCollections();

	kshark_filter_all_entries(kshark_ctx, _rows, _dataSize);

	registerCPUCollections();

	emit updateWidgets(this);
}

/** Register a collection of visible entries for each CPU. */
void KsDataStore::registerCPUCollections()
{
	qInfo() << "@ registerCPUCollections";
	kshark_context *kshark_ctx(nullptr);
	int *streamIds, nCPUs, sd;

	if (!kshark_instance(&kshark_ctx))
		return;

	streamIds = kshark_all_streams(kshark_ctx);
	for (int i = 0; i < kshark_ctx->n_streams; ++i) {
		sd = streamIds[i];

		nCPUs = tep_get_cpus(kshark_ctx->stream[sd]->pevent);
		for (int cpu = 0; cpu < nCPUs; ++cpu) {
			kshark_register_data_collection(kshark_ctx,
							_rows, _dataSize,
							KsUtils::matchCPUVisible,
							sd, &cpu, 1,
							0);
		}
	}

	free(streamIds);
}

void KsDataStore::_unregisterCPUCollections()
{
	kshark_context *kshark_ctx(nullptr);
	int *streamIds, nCPUs, sd;
	qInfo() << "@@ unregisterCPUCollections";
	if (!kshark_instance(&kshark_ctx))
		return;

	streamIds = kshark_all_streams(kshark_ctx);
	for (int i = 0; i < kshark_ctx->n_streams; ++i) {
		sd = streamIds[i];
		if (!kshark_filter_is_set(kshark_ctx, sd))
			continue;

		nCPUs = tep_get_cpus(kshark_ctx->stream[sd]->pevent);
		for (int cpu = 0; cpu < nCPUs; ++cpu) {
			kshark_unregister_data_collection(&kshark_ctx->collections,
							  KsUtils::matchCPUVisible,
							  sd, &cpu, 1);
		}
	}

	free(streamIds);
}

void KsDataStore::_applyIdFilter(int filterId, QVector<int> vec, int sd)
{
	kshark_context *kshark_ctx(nullptr);
	if (!kshark_instance(&kshark_ctx))
		return;

	switch (filterId) {
		case KS_SHOW_EVENT_FILTER:
		case KS_HIDE_EVENT_FILTER:
			kshark_filter_clear(kshark_ctx, sd, KS_SHOW_EVENT_FILTER);
			kshark_filter_clear(kshark_ctx, sd, KS_HIDE_EVENT_FILTER);
			break;
		case KS_SHOW_TASK_FILTER:
		case KS_HIDE_TASK_FILTER:
			kshark_filter_clear(kshark_ctx, sd, KS_SHOW_TASK_FILTER);
			kshark_filter_clear(kshark_ctx, sd, KS_HIDE_TASK_FILTER);
			break;
		case KS_SHOW_CPU_FILTER:
		case KS_HIDE_CPU_FILTER:
			kshark_filter_clear(kshark_ctx, sd, KS_SHOW_CPU_FILTER);
			kshark_filter_clear(kshark_ctx, sd, KS_HIDE_CPU_FILTER);
			break;
		default:
			return;
	}

	for (auto &&val: vec)
		kshark_filter_add_id(kshark_ctx, sd, filterId, val);

	if (!kshark_ctx->n_streams)
		return;

	_unregisterCPUCollections();

	/*
	 * If the advanced event filter is set the data has to be reloaded,
	 * because the advanced filter uses tep_records.
	 */
	if (kshark_ctx->stream[sd]->advanced_event_filter->filters)
		reload();
	else
		kshark_filter_stream_entries(kshark_ctx, sd, _rows, _dataSize);

	registerCPUCollections();

	emit updateWidgets(this);
}

/** Apply Show Task filter. */
void KsDataStore::applyPosTaskFilter(int sd, QVector<int> vec)
{
	kshark_context *kshark_ctx(nullptr);
	int nTasks, *pids;

	if (!kshark_instance(&kshark_ctx))
		return;

	nTasks = kshark_get_task_pids(kshark_ctx, sd, &pids);
	free(pids);
	if (vec.count() == nTasks)
		return;

	_applyIdFilter(KS_SHOW_TASK_FILTER, vec, sd);
}

/** Apply Hide Task filter. */
void KsDataStore::applyNegTaskFilter(int sd, QVector<int> vec)
{
	if (!vec.count())
		return;

	_applyIdFilter(KS_HIDE_TASK_FILTER, vec, sd);
}

/** Apply Show Event filter. */
void KsDataStore::applyPosEventFilter(int sd, QVector<int> vec)
{
	_applyIdFilter(KS_SHOW_EVENT_FILTER, vec, sd);
}

/** Apply Hide Event filter. */
void KsDataStore::applyNegEventFilter(int sd, QVector<int> vec)
{
	if (!vec.count())
		return;

	_applyIdFilter(KS_HIDE_EVENT_FILTER, vec, sd);
}

/** Apply Show CPU filter. */
void KsDataStore::applyPosCPUFilter(int sd, QVector<int> vec)
{
	kshark_context *kshark_ctx(nullptr);
	kshark_data_stream *stream;
	int nCPUs;

	if (!kshark_instance(&kshark_ctx))
		return;

	stream = kshark_get_data_stream(kshark_ctx, sd);
	if (!stream)
		return;

	nCPUs = tep_get_cpus(stream->pevent);
	if (vec.count() == nCPUs)
		return;

	_applyIdFilter(KS_SHOW_CPU_FILTER, vec, sd);
}

/** Apply Hide CPU filter. */
void KsDataStore::applyNegCPUFilter(int sd, QVector<int> vec)
{
	if (!vec.count())
		return;

	_applyIdFilter(KS_HIDE_CPU_FILTER, vec, sd);
}

/** Disable all filters. */
void KsDataStore::clearAllFilters()
{
	kshark_context *kshark_ctx(nullptr);
	int *streamIds, sd;

	if (!kshark_instance(&kshark_ctx) || !kshark_ctx->n_streams)
		return;

	_unregisterCPUCollections();

	streamIds = kshark_all_streams(kshark_ctx);
	for (int i = 0; i < kshark_ctx->n_streams; ++i) {
		sd = streamIds[i];

		kshark_filter_clear(kshark_ctx, sd, KS_SHOW_TASK_FILTER);
		kshark_filter_clear(kshark_ctx, sd, KS_HIDE_TASK_FILTER);
		kshark_filter_clear(kshark_ctx, sd, KS_SHOW_EVENT_FILTER);
		kshark_filter_clear(kshark_ctx, sd, KS_HIDE_EVENT_FILTER);
		kshark_filter_clear(kshark_ctx, sd, KS_SHOW_CPU_FILTER);
		kshark_filter_clear(kshark_ctx, sd, KS_HIDE_CPU_FILTER);

		tep_filter_reset(kshark_ctx->stream[sd]->advanced_event_filter);
	}

	kshark_clear_all_filters(kshark_ctx, _rows, _dataSize);

	free(streamIds);

	emit updateWidgets(this);
}

/**
 * @brief Create Plugin Manager. Use list of plugins declared in the
 *	  CMake-generated header file.
 */
KsPluginManager::KsPluginManager(QWidget *parent)
: QObject(parent)
{
	kshark_context *kshark_ctx(nullptr);
	_parsePluginList();

	if (!kshark_instance(&kshark_ctx))
		return;

	registerFromList(kshark_ctx);
}

KsPluginManager::~KsPluginManager()
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	unregisterFromList(kshark_ctx);
}

/** Parse the plugin list declared in the CMake-generated header file. */
void KsPluginManager::_parsePluginList()
{
	_ksPluginList = KsUtils::getPluginList();
	int nPlugins = _ksPluginList.count();

	_registeredKsPlugins.resize(nPlugins);
	for (int i = 0; i < nPlugins; ++i) {
		if (_ksPluginList[i].contains(" default", Qt::CaseInsensitive)) {
			_ksPluginList[i].remove(" default", Qt::CaseInsensitive);
			_registeredKsPlugins[i] = true;
		} else {
			_registeredKsPlugins[i] = false;
		}
	}
}

/**
 * Register the plugins by using the information in "_ksPluginList" and
 * "_registeredKsPlugins".
 */
void KsPluginManager::registerFromList(kshark_context *kshark_ctx)
{
	qInfo() << "registerFromList" << _ksPluginList;
	auto lamRegBuiltIn = [&kshark_ctx](const QString &plugin)
	{
		char *lib;
		int n;

		n = asprintf(&lib, "%s/lib/plugin-%s.so",
			     KS_DIR, plugin.toStdString().c_str());
		if (n <= 0)
			return;

		qInfo() << "reg" << lib;
		kshark_register_plugin(kshark_ctx, lib);
		free(lib);
	};

	auto lamRegUser = [&kshark_ctx](const QString &plugin)
	{
		std::string lib = plugin.toStdString();
		kshark_register_plugin(kshark_ctx, lib.c_str());
	};

	for (auto const &p: _ksPluginList)
		lamRegBuiltIn(p);

	for (auto const &p: _userPluginList)
		lamRegUser(p);

// 	_forEachInList(_ksPluginList,
// 		       _registeredKsPlugins,
// 		       lamRegBuiltIn);

// 	_forEachInList(_userPluginList,
// 		       _registeredUserPlugins,
// 		       lamRegUser);
}

/**
 * Unegister the plugins by using the information in "_ksPluginList" and
 * "_registeredKsPlugins".
 */
void KsPluginManager::unregisterFromList(kshark_context *kshark_ctx)
{
	auto lamUregBuiltIn = [&kshark_ctx](const QString &plugin)
	{
		char *lib;
		int n;

		n = asprintf(&lib, "%s/lib/plugin-%s.so",
			     KS_DIR, plugin.toStdString().c_str());
		if (n <= 0)
			return;

		qInfo() << "u_reg" << lib;
		kshark_unregister_plugin(kshark_ctx, lib);
		free(lib);
	};

	auto lamUregUser = [&kshark_ctx](const QString &plugin)
	{
		std::string lib = plugin.toStdString();
		kshark_unregister_plugin(kshark_ctx, lib.c_str());
	};

	for (auto const &p: _ksPluginList)
		lamUregBuiltIn(p);

	for (auto const &p: _userPluginList)
		lamUregUser(p);

// 	_forEachInList(_ksPluginList,
// 		       _registeredKsPlugins,
// 			lamUregBuiltIn);

// 	_forEachInList(_userPluginList,
// 		       _registeredUserPlugins,
// 			lamUregUser);
}

/**
 * @brief Register a Plugin.
 *
 * @param plugin: provide here the name of the plugin (as in the CMake-generated
 *		  header file) of a name of the plugin's library file (.so).
 */
void KsPluginManager::registerPlugin(const QString &plugin)
{
	kshark_context *kshark_ctx(nullptr);
	char *lib;
	int n;

	if (!kshark_instance(&kshark_ctx))
		return;

	for (int i = 0; i < _ksPluginList.count(); ++i) {
		if (_ksPluginList[i] == plugin) {
			/*
			 * The argument is the name of the plugin. From the
			 * name get the library .so file.
			 */
			n = asprintf(&lib, "%s/lib/plugin-%s.so",
					KS_DIR, plugin.toStdString().c_str());
			if (n > 0) {
				kshark_register_plugin(kshark_ctx, lib);
				_registeredKsPlugins[i] = true;
				free(lib);
			}

			return;

		} else if (plugin.contains("/lib/plugin-" + _ksPluginList[i],
					   Qt::CaseInsensitive)) {
			/*
			 * The argument is the name of the library .so file.
			 */
			n = asprintf(&lib, "%s", plugin.toStdString().c_str());
			if (n > 0) {
				kshark_register_plugin(kshark_ctx, lib);
				_registeredKsPlugins[i] = true;
				free(lib);
			}

			return;
		}
	}

	/* No plugin with this name in the list. Try to add it anyway. */
	if (plugin.endsWith(".so") && QFileInfo::exists(plugin)) {
		kshark_register_plugin(kshark_ctx,
				       plugin.toStdString().c_str());

		_userPluginList.append(plugin);
		_registeredUserPlugins.append(true);
	} else {
		qCritical() << "ERROR: " << plugin << "cannot be registered!";
	}
}

/** @brief Unregister a Built in KernelShark plugin.
 *<br>
 * WARNING: Do not use this function to unregister User plugins.
 * Instead use directly kshark_unregister_plugin().
 *
 * @param plugin: provide here the name of the plugin (as in the CMake-generated
 *		  header file) or a name of the plugin's library file (.so).
 *
 */
void KsPluginManager::unregisterPlugin(const QString &plugin)
{
	kshark_context *kshark_ctx(nullptr);
	char *lib;
	int n;

	if (!kshark_instance(&kshark_ctx))
		return;

	for (int i = 0; i < _ksPluginList.count(); ++i) {
		if (_ksPluginList[i] == plugin) {
			/*
			 * The argument is the name of the plugin. From the
			 * name get the library .so file.
			 */
			n = asprintf(&lib, "%s/lib/plugin-%s.so", KS_DIR,
				     plugin.toStdString().c_str());
			if (n > 0) {
				kshark_unregister_plugin(kshark_ctx, lib);
				_registeredKsPlugins[i] = false;
				free(lib);
			}

			return;
		} else if (plugin.contains("/lib/plugin-" + _ksPluginList[i],
					    Qt::CaseInsensitive)) {
			/*
			 * The argument is the name of the library .so file.
			 */
			n = asprintf(&lib, "%s", plugin.toStdString().c_str());
			if (n > 0) {
				kshark_unregister_plugin(kshark_ctx, lib);
				_registeredKsPlugins[i] = false;
				free(lib);
			}

			return;
		}
	}
}

#if 0
/** @brief Add to the list and initialize user-provided plugins. All other
 *	   previously loaded plugins will be reinitialized and the data will be
 *	   reloaded.
 *
 * @param fileNames: the library files (.so) of the plugins.
*/
void KsPluginManager::addPlugins(const QStringList &fileNames)
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	kshark_handle_plugins(kshark_ctx, KSHARK_PLUGIN_CLOSE);

	for (auto const &p: fileNames)
		registerPlugin(p);

	kshark_handle_plugins(kshark_ctx, KSHARK_PLUGIN_INIT);

	emit dataReload();
}
#endif

/** Unload all plugins. */
void KsPluginManager::unloadAll()
{
	kshark_context *kshark_ctx(nullptr);
	int *streamIds;

	if (!kshark_instance(&kshark_ctx))
		return;

	streamIds = kshark_all_streams(kshark_ctx);
	for (int i = 0; i < kshark_ctx->n_streams; ++i)
		kshark_handle_all_plugins(kshark_ctx, streamIds[i],
					  KSHARK_PLUGIN_CLOSE);

	unregisterFromList(kshark_ctx);

	kshark_free_plugin_list(kshark_ctx->plugins);
	kshark_ctx->plugins = nullptr;
	kshark_free_event_handler_list(kshark_ctx->event_handlers);
}

/** Unload all plugins. */
void KsPluginManager::unload(int sd)
{
	kshark_context *kshark_ctx(nullptr);

	if (!kshark_instance(&kshark_ctx))
		return;

	kshark_handle_all_plugins(kshark_ctx, sd, KSHARK_PLUGIN_CLOSE);

	kshark_free_plugin_list(kshark_ctx->plugins);
	kshark_ctx->plugins = nullptr;
	kshark_free_event_handler_list(kshark_ctx->event_handlers);

	unregisterFromList(kshark_ctx);
}

/** @brief Update (change) the Plugins.
 *
 * @param pluginIds: The indexes of the plugins to be loaded.
 */
// void KsPluginManager::updatePlugins(QVector<int> pluginIds)
// {
// 	kshark_context *kshark_ctx(nullptr);
// 
// 	if (!kshark_instance(&kshark_ctx))
// 		return;
// 
// 	auto lamRegisterPlugins = [&] (QVector<int> ids)
// 	{
// 		int nKsPlugins = _registeredKsPlugins.count();
// 
// 		/* First clear all registered plugins. */
// 		for (auto &p: _registeredKsPlugins)
// 			p = false;
// 		for (auto &p: _registeredUserPlugins)
// 			p = false;
// 
// 		/* The vector contains the indexes of those to register. */
// 		for (auto const &p: ids) {
// 			if (p < nKsPlugins)
// 				_registeredKsPlugins[p] = true;
// 			else
// 				_registeredUserPlugins[p - nKsPlugins] = true;
// 		}
// 		registerFromList(kshark_ctx);
// 	};
// 
// 	if (!kshark_ctx->pevent) {
// 		kshark_free_plugin_list(kshark_ctx->plugins);
// 		kshark_ctx->plugins = nullptr;
// 
// 		/*
// 		 * No data is loaded. For the moment, just register the
// 		 * plugins. Handling of the plugins will be done after
// 		 * we load a data file.
// 		 */
// 		lamRegisterPlugins(pluginIds);
// 		return;
// 	}
// 
// 	/* Clean up all old plugins first. */
// 	unloadAll();
// 
// 	/* Now load. */
// 	lamRegisterPlugins(pluginIds);
// 	kshark_handle_plugins(kshark_ctx, KSHARK_PLUGIN_INIT);
// 
// 	emit dataReload();
// }
/** @brief Update (change) the Plugins.
 *
 * @param pluginIds: The indexes of the plugins to be loaded.
 */
void KsPluginManager::updatePlugins(int sd, QVector<int> pluginIds)
{
	kshark_context *kshark_ctx(nullptr);
// 	int *streamIds;

	if (!kshark_instance(&kshark_ctx))
		return;

	auto lamRegisterPlugins = [&] (QVector<int> ids)
	{
		int nKsPlugins = _registeredKsPlugins.count();

		/* First clear all registered plugins. */
		for (auto &p: _registeredKsPlugins)
			p = false;
		for (auto &p: _registeredUserPlugins)
			p = false;

		/* The vector contains the indexes of those to register. */
		for (auto const &p: ids) {
			if (p < nKsPlugins)
				_registeredKsPlugins[p] = true;
			else
				_registeredUserPlugins[p - nKsPlugins] = true;
		}
// 		registerFromList(kshark_ctx);
	};

	if (!kshark_ctx->n_streams) {
		kshark_free_plugin_list(kshark_ctx->plugins);
		kshark_ctx->plugins = nullptr;

		/*
		 * No data is loaded. For the moment, just register the
		 * plugins. Handling of the plugins will be done after
		 * we load a data file.
		 */
		lamRegisterPlugins(pluginIds);
		return;
	}

	/* First clean up all old plugins associated with this data stream. */
// 	unload(sd);

	/* Now load. */
	lamRegisterPlugins(pluginIds);

// 	kshark_handle_plugins(kshark_ctx, sd, KSHARK_PLUGIN_INIT);

// 	streamIds = kshark_all_streams(kshark_ctx);
// 	for (int i = 0; i < kshark_ctx->n_streams; ++i)
// 		emit dataReload(streamIds[i]);
// 	free(streamIds);
}

void KsPluginManager::updatePlugins_hack(int sd, QVector<int> pluginIds)
{
	kshark_context *kshark_ctx(nullptr);
	struct kshark_plugin_list *plugin;

	qInfo() << "updatePlugins_hack" << sd << pluginIds;
	if (!kshark_instance(&kshark_ctx))
		return;

	for (plugin = kshark_ctx->plugins; plugin; plugin = plugin->next)
		kshark_plugin_add_stream(plugin, sd);

	kshark_handle_all_plugins(kshark_ctx, sd, KSHARK_PLUGIN_UPDATE);
}
