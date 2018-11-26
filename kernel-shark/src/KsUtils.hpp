/* SPDX-License-Identifier: LGPL-2.1 */

/*
 * Copyright (C) 2017 VMware Inc, Yordan Karadzhov <ykaradzhov@vmware.com>
 */

/**
 *  @file    KsUtils.hpp
 *  @brief   KernelShark Utils.
 */

#ifndef _KS_UTILS_H
#define _KS_UTILS_H

// C++ 11
#include <chrono>

// Qt
#include <QtWidgets>

// KernelShark
#include "libkshark.h"
#include "libkshark-model.h"
#include "KsCmakeDef.hpp"
#include "KsPlotTools.hpp"

/** Macro providing the height of the screen in pixels. */
#define SCREEN_HEIGHT  QApplication::desktop()->screenGeometry().height()

/** Macro providing the width of the screen in pixels. */
#define SCREEN_WIDTH   QApplication::desktop()->screenGeometry().width()

//! @cond Doxygen_Suppress

auto fontHeight = []()
{
	QFont font;
	QFontMetrics fm(font);

	return fm.height();
};

auto stringWidth = [](QString s)
{
	QFont font;
	QFontMetrics fm(font);

	return fm.width(s);
};

//! @endcond

/** Macro providing the height of the font in pixels. */
#define FONT_HEIGHT		fontHeight()

/** Macro providing the width of the font in pixels. */
#define FONT_WIDTH 		stringWidth("4")

/** Macro providing the width of a string in pixels. */
#define STRING_WIDTH(s)		stringWidth(s)

/** Macro providing the height of the KernelShark graphs in pixels. */
#define KS_GRAPH_HEIGHT	(FONT_HEIGHT*2)

//! @cond Doxygen_Suppress

#define KS_JSON_CAST(doc) \
reinterpret_cast<json_object *>(doc)

#define KS_C_STR_CAST(doc) \
reinterpret_cast<const char *>(doc)

typedef std::chrono::high_resolution_clock::time_point  hd_time;

#define GET_TIME std::chrono::high_resolution_clock::now()

#define GET_DURATION(t0) \
std::chrono::duration_cast<std::chrono::duration<double>>( \
std::chrono::high_resolution_clock::now() - t0).count()

//! @endcond

namespace KsUtils {

QVector<int> getPidList(int sd);

QVector<int> getFilterIds(tracecmd_filter_id *filter);

/** @brief Geat the list of plugins. */
inline QStringList getPluginList() {return plugins.split(";");}

void listFilterSync(bool state);

void graphFilterSync(bool state);

QCheckBox *addCheckBoxToMenu(QMenu *menu, QString name);

/** @brief Convert the timestamp of the trace record into a string showing
 *	   the time in seconds.
 *
 * @param ts: Input location for the timestamp.
 * @param prec: the number of digits after the decimal point in the return
 *		string.
 *
 * @returns String showing the time in seconds.
 */
inline QString Ts2String(int64_t ts, int prec)
{
	return QString::number(ts * 1e-9, 'f', prec);
}

bool matchCPUVisible(struct kshark_context *kshark_ctx,
		     struct kshark_entry *e, int sd, int *cpu);

void setElidedText(QLabel* label, QString text,
		   enum Qt::TextElideMode mode,
		   int labelWidth);


#define KS_STREAM_COLOR_LIGHTNESS 30

inline QColor getStreamColor(int sd)
{
	QColor col(Qt::GlobalColor(Qt::red + sd));
	col.setAlpha(KS_STREAM_COLOR_LIGHTNESS);

	return col;
}

}; // KsUtils

/** Identifier of the Dual Marker active state. */
enum class DualMarkerState {
	A,
	B
};

/**
 * The KsDataStore class provides the access to trace data for all KernelShark
 * widgets.
 */
class KsDataStore : public QObject
{
	Q_OBJECT
public:
	explicit KsDataStore(QWidget *parent = nullptr);

	~KsDataStore();

	int loadDataFile(const QString &file);

	int appendDataFile(const QString &file, int64_t shift);

	void clear();

	tep_handle *tep(int sd) const;

	/** Get the trace data array. */
	struct kshark_entry **rows() const {return _rows;}

	struct kshark_entry ***rows_r() {return &_rows;}

	/** Get the size of the data array. */
	size_t size() const {return _dataSize;}

	size_t *size_r() {return &_dataSize;}

	void reload();

	void update();

	void registerCPUCollections();

	void applyPosTaskFilter(int sd, QVector<int> vec);

	void applyNegTaskFilter(int sd, QVector<int> vec);

	void applyPosEventFilter(int sd, QVector<int> vec);

	void applyNegEventFilter(int sd, QVector<int> vec);

	void applyPosCPUFilter(int sd, QVector<int> vec);

	void applyNegCPUFilter(int sd, QVector<int> vec);

	void clearAllFilters();

signals:
	/**
	 * This signal is emitted when the data has changed and the View
	 * widgets have to update.
	 */
	void updateWidgets(KsDataStore *);

private:
	/** Trace data array. */
	struct kshark_entry	**_rows;

	/** The size of the data array. */
	size_t			_dataSize;

	int _openDataFile(kshark_context *kshark_ctx, const QString &file);

	void _freeData();

	void _unregisterCPUCollections();

	void _applyIdFilter(int filterId, QVector<int> vec, int sd);
};

/** A Plugin Manager class. */
class KsPluginManager : public QObject
{
	Q_OBJECT
public:
	explicit KsPluginManager(QWidget *parent = nullptr);

	 ~KsPluginManager();

	/** A list of available built-in plugins. */
	QStringList	_ksPluginList;

	/** A list of registered built-in plugins. */
	QVector<bool>	_registeredKsPlugins;

	/** A list of available user plugins. */
	QStringList	_userPluginList;

	/** A list of registered user plugins. */
	QVector<bool>	_registeredUserPlugins;

	void registerFromList(kshark_context *kshark_ctx);
	void unregisterFromList(kshark_context *kshark_ctx);

	void registerPlugin(const QString &plugin);
	void unregisterPlugin(const QString &plugin);

	void addPlugins(const QStringList &fileNames);

	void unloadAll();
	void unload(int sd);

	void updatePlugins(int sd, QVector<int> pluginId);
	void updatePlugins_hack(int sd, QVector<int> pluginIds);

signals:
	/** This signal is emitted when a plugin is loaded or unloaded. */
	void dataReload(int sd);

private:
	void _parsePluginList();

	template <class T>
	void _forEachInList(const QStringList &pl,
			    const QVector<bool> &reg,
			    T action)
	{
		int nPlugins;
		nPlugins = pl.count();
		for (int i = 0; i < nPlugins; ++i) {
			if (reg[i]) {
				action(pl[i]);
			}
		}
	}
};

KsPlot::Color& operator <<(KsPlot::Color &thisColor, const QColor &c);

#endif
