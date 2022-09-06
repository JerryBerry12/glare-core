/*=====================================================================
QtUtils.h
---------
File created by ClassTemplate on Mon Jun 29 15:03:47 2009
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __QTUTILS_H_666_
#define __QTUTILS_H_666_


#include <QtCore/QString>
class QObject;
class QWidget;
class QCheckBox;
class ArgumentParser;
class QLayout;
class QMainWindow;
namespace Indigo { class String; }


/*=====================================================================
QtUtils
-------

=====================================================================*/


namespace QtUtils
{

/*
	Convert an Indigo string into a QT string.
*/
const QString toQString(const std::string& s);


const QString toQString(const Indigo::String& s);


/*
	Convert a QT string to an std::string.
*/
const std::string toIndString(const QString& s);

/*
	Convert a QT string to an Indigo::String.
*/
const Indigo::String toIndigoString(const QString& s);

/*
	Returns the suppported image filters for loading images,
*/
const QString imageLoadFilters();

void showErrorMessageDialog(const std::string& error_msg, QWidget* parent = NULL);
void showErrorMessageDialog(const QString& error_msg, QWidget* parent = NULL);

/*
backgroundModeCheckBox may be NULL.
*/
void setProcessPriority(const ArgumentParser& parsed_args, QCheckBox* backgroundModeCheckBox);

void ClearLayout(QLayout* layout, bool deleteWidgets);
void RemoveLayout(QWidget* widget);


// Escape a string for HTML. (replace "<" with "&lt;" etc..)
const std::string htmlEscape(const std::string& s);

const std::string urlEscape(const std::string& s);


// Print out widget names and geometry etc.. for widget and its descendents.
void dumpWidgetTreeDebugInfo(QObject* widget);


// If window was minimised, restore to non-minimised state.
void unminimiseWindow(QMainWindow* window);

}


#endif //__QTUTILS_H_666_




