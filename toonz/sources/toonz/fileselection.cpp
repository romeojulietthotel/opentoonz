

#include "fileselection.h"

// Tnz6 includes
#include "convertpopup.h"
#include "filebrowser.h"
#include "filedata.h"
#include "iocommand.h"
#include "menubarcommandids.h"
#include "flipbook.h"
#include "filebrowsermodel.h"
#include "exportscenepopup.h"
#include "tapp.h"

#ifndef LINETEST
#include "batches.h"
#endif

// TnzQt includes
#include "toonzqt/imageutils.h"
#include "toonzqt/dvdialog.h"
#include "toonzqt/infoviewer.h"
#include "toonzqt/icongenerator.h"
#include "toonzqt/gutil.h"
#include "historytypes.h"

// TnzLib includes
#include "toonz/tproject.h"
#include "toonz/toonzscene.h"
#include "toonz/sceneresources.h"
#include "toonz/preferences.h"

// TnzCore includes
#include "tfiletype.h"
#include "tsystem.h"

// Qt includes
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QVariant>
#include <QMainWindow>

using namespace DVGui;

//TODO: spostare i comandi in FileBrowser?

//------------------------------------------------------------------------

namespace
{

//=============================================================================
//  CopyFilesUndo
//-----------------------------------------------------------------------------

class CopyFilesUndo : public TUndo
{
	QMimeData *m_oldData;
	QMimeData *m_newData;

public:
	CopyFilesUndo(QMimeData *oldData,
				  QMimeData *newData)
		: m_oldData(oldData), m_newData(newData)
	{
	}

	void undo() const
	{
		QClipboard *clipboard = QApplication::clipboard();
		clipboard->setMimeData(cloneData(m_oldData), QClipboard::Clipboard);
	}

	void redo() const
	{
		QClipboard *clipboard = QApplication::clipboard();
		clipboard->setMimeData(cloneData(m_newData), QClipboard::Clipboard);
	}

	int getSize() const
	{
		return sizeof(*this);
	}

	QString getHistoryString()
	{
		return QObject::tr("Copy File");
	}
};

//=============================================================================
//  PasteFilesUndo
//-----------------------------------------------------------------------------

class PasteFilesUndo : public TUndo
{
	std::vector<TFilePath> m_newFiles;
	TFilePath m_folder;

public:
	PasteFilesUndo(std::vector<TFilePath> files, TFilePath folder)
		: m_newFiles(files), m_folder(folder)
	{
	}

	~PasteFilesUndo()
	{
	}

	void undo() const
	{
		int i;
		for (i = 0; i < (int)m_newFiles.size(); i++) {
			TFilePath path = m_newFiles[i];
			if (!TSystem::doesExistFileOrLevel(path))
				continue;
			try {
				TSystem::removeFileOrLevel(path);
			} catch (...) {
			}
		}
		FileBrowser::refreshFolder(m_folder);
	}

	void redo() const
	{
		if (!TSystem::touchParentDir(m_folder))
			TSystem::mkDir(m_folder);
		const FileData *data = dynamic_cast<const FileData *>(QApplication::clipboard()->mimeData());
		if (!data)
			return;
		TApp *app = TApp::instance();
		std::vector<TFilePath> files;
		data->getFiles(m_folder, files);
		FileBrowser::refreshFolder(m_folder);
	}

	int getSize() const
	{
		return sizeof(*this);
	}

	QString getHistoryString()
	{
		QString str = QObject::tr("Paste  File  : ");
		for (int i = 0; i < (int)m_newFiles.size(); i++) {
			if (i != 0)
				str += QString(", ");
			str += QString::fromStdString(m_newFiles[i].getLevelName());
		}
		return str;
	}
};

//=============================================================================
//  DuplicateUndo
//-----------------------------------------------------------------------------

class DuplicateUndo : public TUndo
{
	std::vector<TFilePath> m_newFiles;
	std::vector<TFilePath> m_files;

public:
	DuplicateUndo(std::vector<TFilePath> files, std::vector<TFilePath> newFiles)
		: m_files(files), m_newFiles(newFiles)
	{
	}

	~DuplicateUndo()
	{
	}

	void undo() const
	{
		int i;
		for (i = 0; i < (int)m_newFiles.size(); i++) {
			TFilePath path = m_newFiles[i];
			if (!TSystem::doesExistFileOrLevel(path))
				continue;
			if (path.getType() == "tnz")
				TSystem::rmDirTree(path.getParentDir() + (path.getName() + "_files"));
			try {
				TSystem::removeFileOrLevel(path);
			} catch (...) {
			}
		}
		FileBrowser::refreshFolder(m_newFiles[0].getParentDir());
	}

	void redo() const
	{
		int i;
		for (i = 0; i < (int)m_files.size(); i++) {
			TFilePath fp = m_files[i];
			ImageUtils::duplicate(fp);
			FileBrowser::refreshFolder(fp.getParentDir());
		}
		FileBrowser::refreshFolder(m_files[0].getParentDir());
	}

	int getSize() const
	{
		return sizeof(*this);
	}

	QString getHistoryString()
	{
		QString str = QObject::tr("Duplicate  File  : ");
		int i;
		for (i = 0; i < (int)m_files.size(); i++) {
			if (i != 0)
				str += QString(",  ");

			TFilePath fp = m_files[i];
			TFilePath newFp = m_newFiles[i];

			str += QString("%1 > %2")
					   .arg(QString::fromStdString(fp.getLevelName()))
					   .arg(QString::fromStdString(newFp.getLevelName()));
		}
		return str;
	}
};

//-----------------------------------------------------------------------------
}

//------------------------------------------------------------------------

FileSelection::FileSelection()
	: m_exportScenePopup(0)
{
}

FileSelection::~FileSelection()
{
	m_infoViewers.clear();
}

//------------------------------------------------------------------------

void FileSelection::getSelectedFiles(std::vector<TFilePath> &files)
{
	if (!getModel())
		return;
	const std::set<int> &indices = getSelectedIndices();
	std::set<int>::const_iterator it;
	for (it = indices.begin(); it != indices.end(); ++it) {
		QString name = getModel()->getItemData(*it, DvItemListModel::FullPath).toString();
		TFilePath fp(name.toStdWString());
		files.push_back(fp);
	}
}

//------------------------------------------------------------------------

void FileSelection::enableCommands()
{
	DvItemSelection::enableCommands();
	enableCommand(this, MI_DuplicateFile, &FileSelection::duplicateFiles);
	enableCommand(this, MI_Clear, &FileSelection::deleteFiles);
	enableCommand(this, MI_Copy, &FileSelection::copyFiles);
	enableCommand(this, MI_Paste, &FileSelection::pasteFiles);
	enableCommand(this, MI_ShowFolderContents, &FileSelection::showFolderContents);
	enableCommand(this, MI_ConvertFiles, &FileSelection::convertFiles);
	enableCommand(this, MI_AddToBatchRenderList, &FileSelection::addToBatchRenderList);
	enableCommand(this, MI_AddToBatchCleanupList, &FileSelection::addToBatchCleanupList);
	enableCommand(this, MI_CollectAssets, &FileSelection::collectAssets);
	enableCommand(this, MI_ImportScenes, &FileSelection::importScenes);
	enableCommand(this, MI_ExportScenes, &FileSelection::exportScenes);
	enableCommand(this, MI_SelectAll, &FileSelection::selectAll);
}

//------------------------------------------------------------------------

void FileSelection::addToBatchRenderList()
{
#ifndef LINETEST
#ifdef BRAVODEMO
	DVGui::featureNotAvelaible();
#else
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	int i;
	for (i = 0; i < files.size(); i++)
		BatchesController::instance()->addComposerTask(files[i]);

	MsgBox(INFORMATION, QObject::tr(" Task added to the Batch Render List."));
#endif
#endif
}

//------------------------------------------------------------------------

void FileSelection::addToBatchCleanupList()
{
#ifndef LINETEST
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	int i;
	for (i = 0; i < files.size(); i++)
		BatchesController::instance()->addCleanupTask(files[i]);

	MsgBox(INFORMATION, QObject::tr(" Task added to the Batch Cleanup List."));
#endif
}

//------------------------------------------------------------------------

void FileSelection::deleteFiles()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	if (files.empty())
		return;

	QString question;
	if (files.size() == 1) {
		QString fn = QString::fromStdWString(files[0].getWideString());
		question = QObject::tr("Deleting %1. Are you sure?").arg(fn);
	} else {
		question = QObject::tr("Deleting %n files. Are you sure?", "", (int)files.size());
	}
	int ret = MsgBox(question, QObject::tr("Delete"), QObject::tr("Cancel"), 1);
	if (ret == 2 || ret == 0)
		return;

	int i;
	for (i = 0; i < (int)files.size(); i++) {
		TSystem::moveFileOrLevelToRecycleBin(files[i]);
		IconGenerator::instance()->remove(files[i]);
		// TODO: cancellare anche xxxx_files se files[i] == xxxx.tnz
	}
	selectNone();
	FileBrowser::refreshFolder(files[0].getParentDir());
}

//------------------------------------------------------------------------

void FileSelection::copyFiles()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	if (files.empty())
		return;

	QClipboard *clipboard = QApplication::clipboard();
	QMimeData *oldData = cloneData(clipboard->mimeData());
	FileData *data = new FileData();
	data->setFiles(files);
	QApplication::clipboard()->setMimeData(data);
	QMimeData *newData = cloneData(clipboard->mimeData());

	TUndoManager::manager()->add(new CopyFilesUndo(oldData, newData));
}

//------------------------------------------------------------------------

void FileSelection::pasteFiles()
{
	const FileData *data = dynamic_cast<const FileData *>(QApplication::clipboard()->mimeData());
	if (!data)
		return;
	TApp *app = TApp::instance();
	FileBrowser *model = dynamic_cast<FileBrowser *>(getModel());
	if (!model)
		return;
	TFilePath folder = model->getFolder();
	std::vector<TFilePath> newFiles;
	data->getFiles(folder, newFiles);
	FileBrowser::refreshFolder(folder);
	TUndoManager::manager()->add(new PasteFilesUndo(newFiles, folder));
}

//------------------------------------------------------------------------

void FileSelection::showFolderContents()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	TFilePath folderPath;
	if (!files.empty())
		folderPath = files[0].getParentDir();
	if (folderPath.isEmpty()) {
		FileBrowser *model = dynamic_cast<FileBrowser *>(getModel());
		if (!model)
			return;
		folderPath = model->getFolder();
	}
	if (TSystem::isUNC(folderPath))
		QDesktopServices::openUrl(QUrl(QString::fromStdWString(folderPath.getWideString())));
	else
		QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(folderPath.getWideString())));
}

//------------------------------------------------------------------------

extern QWidget *CurrentOpenedBrowser;

void FileSelection::viewFileInfo()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	if (files.empty())
		return;
	int j = 0;
	for (j = 0; j < files.size(); j++) {
		InfoViewer *infoViewer = 0;
		int i;
		for (i = 0; i < m_infoViewers.size(); i++) {
			InfoViewer *v = m_infoViewers.at(i);
			if (!v->isHidden())
				continue;
			infoViewer = v;
			break;
		}
		if (!infoViewer) {
			infoViewer = new InfoViewer(CurrentOpenedBrowser);
			m_infoViewers.append(infoViewer);
		}
		infoViewer->setItem(0, 0, files[j]);
	}
}

//------------------------------------------------------------------------

void FileSelection::viewFile()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	int i = 0;
	for (i = 0; i < files.size(); i++) {
		if (!TFileType::isViewable(TFileType::getInfo(files[0])))
			continue;

		if (Preferences::instance()->isDefaultViewerEnabled() &&
			(files[i].getType() == "mov" || files[i].getType() == "avi" || files[i].getType() == "3gp"))
			QDesktopServices::openUrl(QUrl("file:///" + toQString(files[i])));
		else
			::viewFile(files[i]);
	}
}

//------------------------------------------------------------------------

void FileSelection::convertFiles()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	if (files.empty())
		return;

	static ConvertPopup *popup = new ConvertPopup(false);
	if (popup->isConverting()) {
		DVGui::MsgBox(INFORMATION, QObject::tr("A convertion task is in progress! wait until it stops or cancel it"));
		return;
	}
	popup->setFiles(files);
	popup->exec();
}

//------------------------------------------------------------------------

void FileSelection::premultiplyFiles()
{
	QString question = QObject::tr("You are going to premultiply selected files.\nThe operation cannot be undone: are you sure?");
	int ret = MsgBox(question, QObject::tr("Premultiply"), QObject::tr("Cancel"), 1);
	if (ret == 2 || ret == 0)
		return;

	std::vector<TFilePath> files;
	getSelectedFiles(files);
	int i;
	for (i = 0; i < (int)files.size(); i++) {
		TFilePath fp = files[i];
		ImageUtils::premultiply(fp);
	}
}

//------------------------------------------------------------------------

void FileSelection::duplicateFiles()
{
	std::vector<TFilePath> files;
	std::vector<TFilePath> newFiles;
	getSelectedFiles(files);
	int i;
	for (i = 0; i < (int)files.size(); i++) {
		TFilePath fp = files[i];
		TFilePath newPath = ImageUtils::duplicate(fp);
		newFiles.push_back(newPath);
		FileBrowser::refreshFolder(fp.getParentDir());
	}
	TUndoManager::manager()->add(new DuplicateUndo(files, newFiles));
}

//------------------------------------------------------------------------

int collectAssets(TFilePath scenePath)
{
	// bool dirtyFlag = TNotifier::instance()->getDirtyFlag();
	ToonzScene scene;
	scene.load(scenePath);
	ResourceCollector collector(&scene);
	SceneResources resources(&scene, scene.getXsheet());
	resources.accept(&collector);
	int count = collector.getCollectedResourceCount();
	if (count > 0) {
		scene.save(scenePath);
	}
	// TNotifier::instance()->setDirtyFlag(dirtyFlag);
	return count;
}

//------------------------------------------------------------------------

void FileSelection::collectAssets()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	if (files.empty())
		return;
	int collectedAssets = 0;

	int count = files.size();

	if (count > 1) {
		QMainWindow *mw = TApp::instance()->getMainWindow();
		ProgressDialog progress(tr("Collecting assets..."), tr("Abort"), 0, count, mw);
		progress.setWindowModality(Qt::WindowModal);

		int i;
		for (i = 1; i <= count; i++) {
			collectedAssets += ::collectAssets(files[i - 1]);
			progress.setValue(i);
			if (progress.wasCanceled())
				break;
		}
		progress.setValue(count);
	} else {
		collectedAssets = ::collectAssets(files[0]);
	}
	if (collectedAssets == 0)
		DVGui::MsgBox(INFORMATION, QObject::tr("There are no assets to collect"));
	else if (collectedAssets == 1)
		DVGui::MsgBox(INFORMATION, QObject::tr("One asset imported"));
	else
		DVGui::MsgBox(INFORMATION, QObject::tr("%1 assets imported").arg(collectedAssets));
	DvDirModel::instance()->refreshFolder(TProjectManager::instance()->getCurrentProjectPath().getParentDir());
}

//------------------------------------------------------------------------

int importScene(TFilePath scenePath)
{
	// bool dirtyFlag = TNotifier::instance()->getDirtyFlag();
	ToonzScene scene;
	try {
		IoCmd::loadScene(scene, scenePath, true);
	} catch (TException &e) {
		MsgBox(CRITICAL, QObject::tr("Error loading scene %1 :%2").arg(toQString(scenePath)).arg(QString::fromStdWString(e.getMessage())));
		return 0;
	} catch (...) {
		// TNotifier::instance()->notify(TGlobalChange(true));
		MsgBox(CRITICAL, QObject::tr("Error loading scene %1").arg(toQString(scenePath)));
		return 0;
	}

	try {
		scene.save(scene.getScenePath());
	} catch (TException&) {
		MsgBox(CRITICAL, QObject::tr("There was an error saving the %1 scene.").arg(toQString(scenePath)));
		return 0;
	}

	DvDirModel::instance()->refreshFolder(TProjectManager::instance()->getCurrentProjectPath().getParentDir());

	// TNotifier::instance()->setDirtyFlag(dirtyFlag);
	return 1;
}

//------------------------------------------------------------------------

void FileSelection::importScenes()
{
#ifdef BRAVODEMO
	DVGui::featureNotAvelaible();

#else
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	if (files.empty())
		return;
	int importedSceneCount = 0;

	int count = files.size();

	if (count > 1) {
		QMainWindow *mw = TApp::instance()->getMainWindow();
		ProgressDialog progress(tr("Importing scenes..."), tr("Abort"), 0, count, mw);
		progress.setWindowModality(Qt::WindowModal);

		int i;
		for (i = 1; i <= count; i++) {
			int ret = ::importScene(files[i - 1]);
			importedSceneCount += ret;
			progress.setValue(i);
			if (progress.wasCanceled())
				break;
		}
		progress.setValue(count);
	} else {
		int ret = ::importScene(files[0]);
		importedSceneCount += ret;
	}
	if (importedSceneCount == 0)
		DVGui::MsgBox(INFORMATION, QObject::tr("No scene imported"));
	else if (importedSceneCount == 1)
		DVGui::MsgBox(INFORMATION, QObject::tr("One scene imported"));
	else
		DVGui::MsgBox(INFORMATION, QString::number(importedSceneCount) + QObject::tr("%1 scenes imported").arg(importedSceneCount));
#endif
}
//------------------------------------------------------------------------

void FileSelection::exportScenes()
{
	std::vector<TFilePath> files;
	getSelectedFiles(files);
	if (!m_exportScenePopup)
		m_exportScenePopup = new ExportScenePopup(files);
	else
		m_exportScenePopup->setScenes(files);
	m_exportScenePopup->show();
}

//------------------------------------------------------------------------

void FileSelection::selectAll()
{
	DvItemSelection::selectAll();
	const std::set<int> &indices = getSelectedIndices();
	if (indices.size()) {
		std::set<int>::const_iterator it;
		it = indices.begin();
		QString name = getModel()->getItemData(*it, DvItemListModel::FullPath).toString();
		TFilePath fp(name.toStdWString());
		FileBrowser::refreshFolder(fp.getParentDir());
	}
}