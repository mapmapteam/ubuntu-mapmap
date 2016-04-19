/*
 * MainWindow.cpp
 *
 * (c) 2013 Sofian Audry -- info(@)sofianaudry(.)com
 * (c) 2013 Alexandre Quessy -- alexandre(@)quessy(.)net
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "MainWindow.h"
#include "Commands.h"
#include "ProjectWriter.h"
#include "ProjectReader.h"
#include <sstream>
#include <string>

MM_BEGIN_NAMESPACE

MainWindow::MainWindow()
{
  // Create model.
#if QT_VERSION >= 0x050500
  QMessageLogger(__FILE__, __LINE__, 0).info() << "Video support: " << (Video::hasVideoSupport() ? "yes" : "no");
#else
  QMessageLogger(__FILE__, __LINE__, 0).debug() << "Video support: " << (Video::hasVideoSupport() ? "yes" : "no");
#endif

  mappingManager = new MappingManager;

  // Initialize internal variables.
  currentPaintId = NULL_UID;
  currentMappingId = NULL_UID;
  // TODO: not sure we need this anymore since we have NULL_UID
  _hasCurrentPaint = false;
  _hasCurrentMapping = false;
  currentSelectedItem = NULL;

  // Play state.
  _isPlaying = false;

  // Editing toggles.
  _displayControls = true;
  _stickyVertices = true;
  _displayUndoStack = false;
  _showMenuBar = true; // Show menubar by default

  // UndoStack
  undoStack = new QUndoStack(this);

  // Create everything.
  createLayout();
  createActions();
  createMenus();
  createMappingContextMenu();
  createPaintContextMenu();
  createToolBars();
  createStatusBar();
  updateRecentFileActions();
  updateRecentVideoActions();

  // Load settings.
  readSettings();

  // Start osc.
  startOscReceiver();

  // Defaults.
  setWindowIcon(QIcon(":/mapmap-logo"));
  setCurrentFile("");

  // Create and start timer.
  videoTimer = new QTimer(this);
  videoTimer->setInterval( int( 1000 / MM::FRAMES_PER_SECOND ) );
  connect(videoTimer, SIGNAL(timeout()), this, SLOT(updateCanvases()));
  videoTimer->start();

  // Start playing by default.
  play();

  // after readSettings():
  _preferences_dialog = new PreferencesDialog(this, this);
}

MainWindow::~MainWindow()
{
  delete mappingManager;
  //  delete _facade;
#ifdef HAVE_OSC
  delete osc_timer;
#endif // ifdef
}

void MainWindow::handlePaintItemSelectionChanged()
{
  // Set current paint.
  QListWidgetItem* item = paintList->currentItem();
  currentSelectedItem = item;

  // Is a paint item selected?
  bool paintItemSelected = (item ? true : false);

  if (paintItemSelected)
  {
    // Set current paint.
    uid paintId = getItemId(*item);
    // Unselect current mapping.
    if (currentPaintId != paintId)
      removeCurrentMapping();
    // Set current paint.
    setCurrentPaint(paintId);
  }
  else
    removeCurrentPaint();

  // Enable/disable creation of mappings depending on whether a paint is selected.
  addMeshAction->setEnabled(paintItemSelected);
  addTriangleAction->setEnabled(paintItemSelected);
  addEllipseAction->setEnabled(paintItemSelected);
  // Enable some menus and buttons
  sourceCanvasToolbar->enableZoomToolBar(paintItemSelected);
  destinationCanvasToolbar->enableZoomToolBar(paintItemSelected);
  sourceMenu->setEnabled(paintItemSelected);

  // Update canvases.
  updateCanvases();
}

void MainWindow::handleMappingItemSelectionChanged(const QModelIndex &index)
{
  // Set current paint and mappings.
  uid mappingId = mappingListModel->getItemId(index);
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);
  uid paintId = mapping->getPaint()->getId();
  // Set current mapping and paint
  setCurrentMapping(mappingId);
  setCurrentPaint(paintId);

  // Update canvases.
  updateCanvases();
}

void MainWindow::handleMappingItemChanged(const QModelIndex &index)
{
  // Get item.
  uid mappingId = mappingListModel->getItemId(index);

  // Sync name.
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);
  Q_CHECK_PTR(mapping);

  // Change properties.
  mapping->setName(index.data(Qt::EditRole).toString());
  mapping->setVisible(index.data(Qt::CheckStateRole).toBool());
  mapping->setSolo(index.data(Qt::CheckStateRole + 1).toBool());
  mapping->setLocked(index.data(Qt::CheckStateRole + 2).toBool());
 }

void MainWindow::handleMappingIndexesMoved()
{
  // Reorder mappings.
  QVector<uid> newOrder;
  for (int row=mappingListModel->rowCount()-1; row>=0; row--)
  {
    uid layerId = mappingListModel->getIndexFromRow(row).data(Qt::UserRole).toInt();
    newOrder.push_back(layerId);
  }
  mappingManager->reorderMappings(newOrder);

  // Update canvases according to new order.
  updateCanvases();
}

void MainWindow::handlePaintItemSelected(QListWidgetItem* item)
{
  Q_UNUSED(item);
  // Change currently selected item.
  currentSelectedItem = item;
}

void MainWindow::handlePaintChanged(Paint::ptr paint)
{
  // Change currently selected item.
  uid curMappingId = getCurrentMappingId();
  removeCurrentMapping();
  removeCurrentPaint();

  uid paintId = mappingManager->getPaintId(paint);

  if (paint->getType() == "media")
  {
    QSharedPointer<Video> media = qSharedPointerCast<Video>(paint);
    Q_CHECK_PTR(media);
    updatePaintItem(paintId, media->getIcon(), strippedName(media->getUri()));
    //    QString fileName = QFileDialog::getOpenFileName(this,
    //        tr("Import media source file"), ".");
    //    // Restart video playback. XXX Hack
    //    if (!fileName.isEmpty())
    //      importMediaFile(fileName, paint, false);
  }
  if (paint->getType() == "image")
  {
    QSharedPointer<Image> image = qSharedPointerCast<Image>(paint);
    Q_CHECK_PTR(image);
    updatePaintItem(paintId, image->getIcon(), strippedName(image->getUri()));
    //    QString fileName = QFileDialog::getOpenFileName(this,
    //        tr("Import media source file"), ".");
    //    // Restart video playback. XXX Hack
    //    if (!fileName.isEmpty())
    //      importMediaFile(fileName, paint, true);
  }
  else if (paint->getType() == "color")
  {
    // Pop-up color-choosing dialog to choose color paint.
    QSharedPointer<Color> color = qSharedPointerCast<Color>(paint);
    Q_CHECK_PTR(color);
    updatePaintItem(paintId, color->getIcon(), strippedName(color->getColor().name()));
  }

  if (curMappingId != NULL_UID)
  {
    setCurrentMapping(curMappingId);
  }
}

void MainWindow::mappingPropertyChanged(uid id, QString propertyName, QVariant value)
{
  // Retrieve mapping.
  Mapping::ptr mapping = mappingManager->getMappingById(id);
  Q_CHECK_PTR(mapping);

  // Send to mapping gui.
  MappingGui::ptr mappingGui = getMappingGuiByMappingId(id);
  Q_CHECK_PTR(mappingGui);
  mappingGui->setValue(propertyName, value);

  // Send to actions.
  if (mapping == getCurrentMapping())
  {
    if (propertyName == "solo")
    {
      mappingSoloAction->setChecked(value.toBool());
    }
    else if (propertyName == "locked")
    {
      mappingLockedAction->setChecked(value.toBool());
    }
    else if (propertyName == "visible")
    {
      mappingHideAction->setChecked(!value.toBool());
    }
  }

  // Send to list items.
  if (propertyName == "name")
    mappingListModel->getIndexFromId(id).data(Qt::EditRole).setValue(mapping->getName());
}

void MainWindow::paintPropertyChanged(uid id, QString propertyName, QVariant value)
{
  // Retrieve paint.
  Paint::ptr paint = mappingManager->getPaintById(id);
  Q_CHECK_PTR(paint);

  // Send to paint gui.
  PaintGui::ptr paintGui = getPaintGuiByPaintId(id);
  Q_CHECK_PTR(paintGui);

  paintGui->setValue(propertyName, value);

  // Send to list items.
  QListWidgetItem* paintItem = getItemFromId(*paintList, id);
  if (propertyName == "name")
    paintItem->setText(paint->getName());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  // Stop video playback to avoid lags. XXX Hack
  videoTimer->stop();

  // Popup dialog allowing the user to save before closing.
  if (okToContinue())
  {
    // Save settings
    writeSettings();
    // Close all top level widgets
    foreach (QWidget *widget, QApplication::topLevelWidgets()) {
      if (widget != this) { // Avoid recursion
        widget->close();
      }
    }
    event->accept();
  }
  else
  {
    event->ignore();
  }

  // Restart video playback. XXX Hack
  videoTimer->start();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
#ifdef Q_OS_OSX // On Mac OS X
  Q_UNUSED(event);
  // Do nothing
#endif

#ifdef Q_OS_LINUX // On Linux
  if (event->modifiers() & Qt::AltModifier) {
    QString currentDesktop = QString(getenv("XDG_CURRENT_DESKTOP")).toLower();
    if (currentDesktop != "unity" && !_showMenuBar) {
      menuBar()->setHidden(!menuBar()->isHidden());
      menuBar()->setFocus(Qt::MenuBarFocusReason);
    }
  }
#endif
#ifdef Q_OS_WIN
  if (event->modifiers() & Qt::AltModifier) {
    if (!_showMenuBar) {
      menuBar()->setHidden(!menuBar()->isHidden());
      menuBar()->setFocus(Qt::MenuBarFocusReason);
    }
  }
#endif
}

void MainWindow::setOutputWindowFullScreen(bool enable)
{
  outputWindow->setFullScreen(enable);
  // setCheckState
  displayControlsAction->setChecked(enable);
}

void MainWindow::newFile()
{
  // Stop video playback to avoid lags. XXX Hack
  videoTimer->stop();

  // Popup dialog allowing the user to save before creating a new file.
  if (okToContinue())
  {
    clearWindow();
    setCurrentFile("");
    undoStack->clear();
  }

  // Restart video playback. XXX Hack
  videoTimer->start();
}

void MainWindow::open()
{
  // Stop video playback to avoid lags. XXX Hack
  videoTimer->stop();

  // Popup dialog allowing the user to save before opening a new file.
  if (okToContinue())
  {
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Open project"),
                                                    settings.value("defaultProjectDir").toString(),
                                                    tr("MapMap files (*.%1)").arg(MM::FILE_EXTENSION));
    if (! fileName.isEmpty())
      loadFile(fileName);
  }

  // Restart video playback. XXX Hack
  videoTimer->start();
}

void MainWindow::preferences()
{
  this->_preferences_dialog->show();
}

bool MainWindow::save()
{
  // Popup save-as dialog if file has never been saved.
  if (curFile.isEmpty())
  {
    return saveAs();
  }
  else
  {
    return saveFile(curFile);
  }
}

bool MainWindow::saveAs()
{
  // Stop video playback to avoid lags. XXX Hack
  videoTimer->stop();

  // Popul file dialog to choose filename.
  QString fileName = QFileDialog::getSaveFileName(this,
                                                  tr("Save project"), settings.value("defaultProjectDir").toString(),
                                                  tr("MapMap files (*.%1)").arg(MM::FILE_EXTENSION));

  // Restart video playback. XXX Hack
  videoTimer->start();

  if (fileName.isEmpty())
    return false;
  
  if (! fileName.endsWith(MM::FILE_EXTENSION))
  {
    std::cout << "filename doesn't end with expected extension: " <<
                 fileName.toStdString() << std::endl;
    fileName.append(".");
    fileName.append(MM::FILE_EXTENSION);
  }

  // Save to filename.
  return saveFile(fileName);
}

void MainWindow::importMedia()
{
  // Stop video playback to avoid lags. XXX Hack
  videoTimer->stop();

  // Pop-up file-choosing dialog to choose media file.
  // TODO: restrict the type of files that can be imported
  QString fileName = QFileDialog::getOpenFileName(this,
                                                  tr("Import media source file"),
                                                  settings.value("defaultVideoDir").toString(),
                                                  tr("Media files (%1 %2);;All files (*)")
                                                  .arg(MM::VIDEO_FILES_FILTER)
                                                  .arg(MM::IMAGE_FILES_FILTER));
  // Restart video playback. XXX Hack
  videoTimer->start();

  // Check if file is image or not
  // according to file extension
  if (!fileName.isEmpty()) {
    if (MM::IMAGE_FILES_FILTER.contains(QFileInfo(fileName).suffix(), Qt::CaseInsensitive))
      importMediaFile(fileName, true);
    else
      importMediaFile(fileName, false);
  }
}

void MainWindow::addColor()
{
  // Stop video playback to avoid lags. XXX Hack
  videoTimer->stop();

  // Pop-up color-choosing dialog to choose color paint.
  // FIXME: we use a static variable to store the last chosen color
  // it should rather be a member of this class, or so.
  static QColor color = QColor(0, 255, 0, 255);
  color = QColorDialog::getColor(color, this, tr("Select Color"),
                                 // QColorDialog::DontUseNativeDialog |
                                 QColorDialog::ShowAlphaChannel);
  if (color.isValid())
  {
    addColorPaint(color);
  }

  // Restart video playback. XXX Hack
  videoTimer->start();
}

void MainWindow::addMesh()
{
  // A paint must be selected to add a mapping.
  if (getCurrentPaintId() == NULL_UID)
    return;

  // Retrieve current paint (as texture).
  Paint::ptr paint = getMappingManager().getPaintById(getCurrentPaintId());
  Q_CHECK_PTR(paint);

  // Create input and output quads.
  Mapping* mappingPtr;
  if (paint->getType() == "color")
  {
    MShape::ptr outputQuad = MShape::ptr(Util::createQuadForColor(sourceCanvas->width(), sourceCanvas->height()));
    mappingPtr = new ColorMapping(paint, outputQuad);
  }
  else
  {
    QSharedPointer<Texture> texture = qSharedPointerCast<Texture>(paint);
    Q_CHECK_PTR(texture);

    MShape::ptr outputQuad = MShape::ptr(Util::createMeshForTexture(texture.data(), sourceCanvas->width(), sourceCanvas->height()));
    MShape::ptr  inputQuad = MShape::ptr(Util::createMeshForTexture(texture.data(), sourceCanvas->width(), sourceCanvas->height()));
    mappingPtr = new TextureMapping(paint, outputQuad, inputQuad);
  }

  // Create texture mapping.
  Mapping::ptr mapping(mappingPtr);
  uint mappingId = mappingManager->addMapping(mapping);

  // Lets the undo-stack handle Undo/Redo the adding of mapping item.
  undoStack->push(new AddShapesCommand(this, mappingId));
}

void MainWindow::addTriangle()
{
  // A paint must be selected to add a mapping.
  if (getCurrentPaintId() == NULL_UID)
    return;

  // Retrieve current paint (as texture).
  Paint::ptr paint = getMappingManager().getPaintById(getCurrentPaintId());
  Q_CHECK_PTR(paint);

  // Create input and output quads.
  Mapping* mappingPtr;
  if (paint->getType() == "color")
  {
    MShape::ptr outputTriangle = MShape::ptr(Util::createTriangleForColor(sourceCanvas->width(), sourceCanvas->height()));
    mappingPtr = new ColorMapping(paint, outputTriangle);
  }
  else
  {
    QSharedPointer<Texture> texture = qSharedPointerCast<Texture>(paint);
    Q_CHECK_PTR(texture);

    MShape::ptr outputTriangle = MShape::ptr(Util::createTriangleForTexture(texture.data(), sourceCanvas->width(), sourceCanvas->height()));
    MShape::ptr inputTriangle = MShape::ptr(Util::createTriangleForTexture(texture.data(), sourceCanvas->width(), sourceCanvas->height()));
    mappingPtr = new TextureMapping(paint, inputTriangle, outputTriangle);
  }

  // Create mapping.
  Mapping::ptr mapping(mappingPtr);
  uint mappingId = mappingManager->addMapping(mapping);

  // Lets undo-stack handle Undo/Redo the adding of mapping item.
  undoStack->push(new AddShapesCommand(this, mappingId));
}

void MainWindow::addEllipse()
{
  // A paint must be selected to add a mapping.
  if (getCurrentPaintId() == NULL_UID)
    return;

  // Retrieve current paint (as texture).
  Paint::ptr paint = getMappingManager().getPaintById(getCurrentPaintId());
  Q_CHECK_PTR(paint);

  // Create input and output ellipses.
  Mapping* mappingPtr;
  if (paint->getType() == "color")
  {
    MShape::ptr outputEllipse = MShape::ptr(Util::createEllipseForColor(sourceCanvas->width(), sourceCanvas->height()));
    mappingPtr = new ColorMapping(paint, outputEllipse);
  }
  else
  {
    QSharedPointer<Texture> texture = qSharedPointerCast<Texture>(paint);
    Q_CHECK_PTR(texture);

    MShape::ptr outputEllipse = MShape::ptr(Util::createEllipseForTexture(texture.data(), sourceCanvas->width(), sourceCanvas->height()));
    MShape::ptr inputEllipse = MShape::ptr(Util::createEllipseForTexture(texture.data(), sourceCanvas->width(), sourceCanvas->height()));
    mappingPtr = new TextureMapping(paint, inputEllipse, outputEllipse);
  }

  // Create mapping.
  Mapping::ptr mapping(mappingPtr);
  uint mappingId = mappingManager->addMapping(mapping);

  // Lets undo-stack handle Undo/Redo the adding of mapping item.
  undoStack->push(new AddShapesCommand(this, mappingId));
}

void MainWindow::about()
{
  // Stop video playback to avoid lags. XXX Hack
  videoTimer->stop();

  // Pop-up about dialog.
  QMessageBox::about(this, tr("About MapMap"),
                     tr("<h2><img src=\":mapmap-title\"/> %1</h2>"
                        "<p>Copyright &copy; 2013 %2.</p>"
                        "<p>MapMap is a free software for video mapping.</p>"
                        "<p>Projection mapping, also known as video mapping and spatial augmented reality, "
                        "is a projection technology used to turn objects, often irregularly shaped, into "
                        "a display surface for video projection. These objects may be complex industrial "
                        "landscapes, such as buildings. By using specialized software, a two or three "
                        "dimensional object is spatially mapped on the virtual program which mimics the "
                        "real environment it is to be projected on. The software can interact with a "
                        "projector to fit any desired image onto the surface of that object. This "
                        "technique is used by artists and advertisers alike who can add extra dimensions, "
                        "optical illusions, and notions of movement onto previously static objects. The "
                        "video is commonly combined with, or triggered by, audio to create an "
                        "audio-visual narrative."
                        "This project was made possible by the support of the International Organization of "
                        "La Francophonie.</p>"
                        "<p>http://mapmap.info<br />"
                        "http://www.francophonie.org</p>"
                        ).arg(MM::VERSION, MM::COPYRIGHT_OWNERS));

  // Restart video playback. XXX Hack
  videoTimer->start();
}

void MainWindow::updateStatusBar()
{
  QPointF mousePos = destinationCanvas->mapToScene(destinationCanvas->mapFromGlobal(destinationCanvas->cursor().pos()));
  if (currentSelectedItem) // Show mouse coordinate only if mappingList is not empty
    mousePosLabel->setText("Mouse coordinate:   X " + QString::number(mousePos.x()) + "   Y " + QString::number(mousePos.y()));
  else
    mousePosLabel->setText(""); // Otherwise set empty text.
  currentMessageLabel->setText(statusBar()->currentMessage());
  sourceZoomLabel->setText("Source: " + QString::number(int(sourceCanvas->getZoomFactor() * 100)).append(QChar('%')));
  destinationZoomLabel->setText("Destination: " + QString::number(int(destinationCanvas->getZoomFactor() * 100)).append(QChar('%')));
  undoLabel->setText(undoStack->text(undoStack->count() - 1));
}

void MainWindow::showMenuBar(bool shown)
{
  _showMenuBar = shown;

#ifdef Q_OS_OSX // On Mac OS X
  // Do nothing
#endif
#ifdef Q_OS_LINUX // On Linux
  QString currentDesktop = QString(getenv("XDG_CURRENT_DESKTOP")).toLower();
  if (currentDesktop != "unity")
    menuBar()->setVisible(shown);
#endif
#ifdef Q_OS_WIN // On Windows
    menuBar()->setVisible(shown);
#endif
}

/**
 * Called when the user wants to delete an item.
 *
 * Deletes either a Paint or a Mapping.
 */
void MainWindow::deleteItem()
{
  bool isMappingTabSelected = (mappingSplitter == contentTab->currentWidget());
  bool isPaintTabSelected = (paintSplitter == contentTab->currentWidget());

  if (currentSelectedItem)
  {
    if (isMappingTabSelected) //currentSelectedItem->listWidget() == mappingList)
    {
      // Delete mapping.
      undoStack->push(new DeleteMappingCommand(this, getCurrentMappingId()));
      //currentSelectedItem = NULL;
    }
    else if (isPaintTabSelected) //currentSelectedItem->listWidget() == paintList)
    {
      // Delete paint.
      undoStack->push(new RemovePaintCommand(this, getItemId(*paintList->currentItem())));
      //currentSelectedItem = NULL;
    }
    else
    {
      qCritical() << "Selected item neither a mapping nor a paint." << endl;
    }
  }
}

void MainWindow::duplicateMappingItem()
{
  if (currentSelectedIndex.isValid())
  {
    duplicateMapping(currentMappingItemId());
  }
  else
  {
    qCritical() << "No selected mapping" << endl;
  }
}

void MainWindow::deleteMappingItem()
{
  if (currentSelectedIndex.isValid())
  {
    undoStack->push(new DeleteMappingCommand(this, currentMappingItemId()));
  }
  else
  {
    qCritical() << "No selected mapping" << endl;
  }
}

void MainWindow::renameMappingItem()
{
  // Set current item editable and rename it
  QModelIndex index = mappingList->currentIndex();
  // Used by context menu
  mappingList->edit(index);
  // Switch to mapping tab.
  contentTab->setCurrentWidget(mappingSplitter);
}

void MainWindow::setMappingItemLocked(bool locked)
{
  setMappingLocked(currentMappingItemId(), locked);
}

void MainWindow::setMappingItemHide(bool hide)
{
  setMappingVisible(currentMappingItemId(), !hide);
}

void MainWindow::setMappingItemSolo(bool solo)
{
  setMappingSolo(currentMappingItemId(), solo);
}

void MainWindow::renameMapping(uid mappingId, const QString &name)
{
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);
  Q_CHECK_PTR(mapping);
  
  if (!mapping.isNull()) {
    QModelIndex index = mappingListModel->getIndexFromId(mappingId);
    mappingListModel->setData(index, name, Qt::EditRole);
    mapping->setName(name);
  }
}

//void MainWindow::mappingListEditEnd(QWidget *editor)
//{
//  QString name = reinterpret_cast<QLineEdit*>(editor)->text();
//  renameMapping(getItemId(*mappingList->currentItem()), name);
//}

void MainWindow::deletePaintItem()
{
  if(currentSelectedItem)
  {
    undoStack->push(new RemovePaintCommand(this, getItemId(*paintList->currentItem())));
  }
  else
  {
    qCritical() << "No selected paint" << endl;
  }
}

void MainWindow::renamePaintItem()
{
  // Set current item editable and rename it
  QListWidgetItem* item = paintList->currentItem();
  item->setFlags(item->flags() | Qt::ItemIsEditable);
  // Used by context menu
  paintList->editItem(item);
  // Switch to paint tab
  contentTab->setCurrentWidget(paintSplitter);
}

void MainWindow::renamePaint(uid paintId, const QString &name)
{
  Paint::ptr paint = mappingManager->getPaintById(paintId);
  Q_CHECK_PTR(paint);
  if (!paint.isNull()) {
    paint->setName(name);
  }
}

void MainWindow::paintListEditEnd(QWidget *editor)
{
  QString name = reinterpret_cast<QLineEdit*>(editor)->text();
  renamePaint(getItemId(*paintList->currentItem()), name);
}

void MainWindow::openRecentFile()
{
  QAction *action = qobject_cast<QAction *>(sender());
  if (action)
    loadFile(action->data().toString());
}

void MainWindow::openRecentVideo()
{
  QAction *action = qobject_cast<QAction *>(sender());
  if (action)
    importMediaFile(action->data().toString(),false);
}

bool MainWindow::clearProject()
{
  // Disconnect signals to avoid problems when clearning mappingList and paintList.
  disconnectProjectWidgets();

  // Clear current paint / mapping.
  removeCurrentPaint();
  removeCurrentMapping();

  // Empty list widgets.
  mappingListModel->clear();
  paintList->clear();

  // Clear property panel.
  for (int i=mappingPropertyPanel->count()-1; i>=0; i--)
    mappingPropertyPanel->removeWidget(mappingPropertyPanel->widget(i));

  // Disable property panel.
  mappingPropertyPanel->setDisabled(true);

  // Clear list of mappers.
  mappers.clear();

  // Clear list of paint guis.
  paintGuis.clear();

  // Clear model.
  mappingManager->clearAll();

  // Refresh GL canvases to clear them out.
  sourceCanvas->repaint();
  destinationCanvas->repaint();

  // Reconnect everything.
  connectProjectWidgets();

  // Window was modified.
  windowModified();

  return true;
}

uid MainWindow::createMediaPaint(uid paintId, QString uri, float x, float y,
                                 bool isImage, bool live, double rate)
{
  // Cannot create image with already existing id.
  if (Paint::getUidAllocator().exists(paintId))
    return NULL_UID;

  else
  {
    // Check if file exists before
    if (! fileExists(uri))
      uri = locateMediaFile(uri, isImage);

    Texture* tex = 0;
    if (isImage)
      tex = new Image(uri, paintId);
    else {
      tex = new Video(uri, live, rate, paintId);
    }

    // Create new image with corresponding ID.
    tex->setPosition(x, y);

    // Add it to the manager.
    Paint::ptr paint(tex);
    paint->setName(strippedName(uri));

    // Add paint to model and return its uid.
    uid id = mappingManager->addPaint(paint);

    // Add paint widget item.
    undoStack->push(new AddPaintCommand(this, id, paint->getIcon(), paint->getName()));
    return id;
  }
}

uid MainWindow::createColorPaint(uid paintId, QColor color)
{
  // Cannot create image with already existing id.
  if (Paint::getUidAllocator().exists(paintId))
    return NULL_UID;

  else
  {
    Color* img = new Color(color, paintId);

    // Add it to the manager.
    Paint::ptr paint(img);
    paint->setName(strippedName(color.name()));

    // Add paint to model and return its uid.
    uid id = mappingManager->addPaint(paint);

    // Add paint widget item.
    undoStack->push(new AddPaintCommand(this, id, paint->getIcon(), paint->getName()));

    return id;
  }
}

uid MainWindow::createMeshTextureMapping(uid mappingId,
                                         uid paintId,
                                         int nColumns, int nRows,
                                         const QVector<QPointF> &src, const QVector<QPointF> &dst)
{
  // Cannot create element with already existing id or element for which no paint exists.
  if (Mapping::getUidAllocator().exists(mappingId) ||
      !Paint::getUidAllocator().exists(paintId) ||
      paintId == NULL_UID)
    return NULL_UID;

  else
  {
    Paint::ptr paint = mappingManager->getPaintById(paintId);
    int nVertices = nColumns * nRows;
    qDebug() << nVertices << " vs " << nColumns << "x" << nRows << " vs " << src.size() << " " << dst.size() << endl;
    Q_ASSERT(src.size() == nVertices && dst.size() == nVertices);

    MShape::ptr inputMesh( new Mesh(src, nColumns, nRows));
    MShape::ptr outputMesh(new Mesh(dst, nColumns, nRows));

    // Add it to the manager.
    Mapping::ptr mapping(new TextureMapping(paint, outputMesh, inputMesh, mappingId));
    uid id = mappingManager->addMapping(mapping);

    // Add it to the GUI.
    addMappingItem(mappingId);

    // Return the id.
    return id;
  }
}

uid MainWindow::createTriangleTextureMapping(uid mappingId,
                                             uid paintId,
                                             const QVector<QPointF> &src, const QVector<QPointF> &dst)
{
  // Cannot create element with already existing id or element for which no paint exists.
  if (Mapping::getUidAllocator().exists(mappingId) ||
      !Paint::getUidAllocator().exists(paintId) ||
      paintId == NULL_UID)
    return NULL_UID;

  else
  {
    Paint::ptr paint = mappingManager->getPaintById(paintId);
    Q_ASSERT(src.size() == 3 && dst.size() == 3);

    MShape::ptr inputTriangle( new Triangle(src[0], src[1], src[2]));
    MShape::ptr outputTriangle(new Triangle(dst[0], dst[1], dst[2]));

    // Add it to the manager.
    Mapping::ptr mapping(new TextureMapping(paint, outputTriangle, inputTriangle, mappingId));
    uid id = mappingManager->addMapping(mapping);

    // Add it to the GUI.
    addMappingItem(mappingId);

    // Return the id.
    return id;
  }
}

uid MainWindow::createEllipseTextureMapping(uid mappingId,
                                            uid paintId,
                                            const QVector<QPointF> &src, const QVector<QPointF> &dst)
{
  // Cannot create element with already existing id or element for which no paint exists.
  if (Mapping::getUidAllocator().exists(mappingId) ||
      !Paint::getUidAllocator().exists(paintId) ||
      paintId == NULL_UID)
    return NULL_UID;

  else
  {
    Paint::ptr paint = mappingManager->getPaintById(paintId);
    Q_ASSERT(src.size() == 5 && dst.size() == 5);

    MShape::ptr inputEllipse( new Ellipse(src[0], src[1], src[2], src[3], src[4]));
    MShape::ptr outputEllipse(new Ellipse(dst[0], dst[1], dst[2], dst[3], dst[4]));

    // Add it to the manager.
    Mapping::ptr mapping(new TextureMapping(paint, outputEllipse, inputEllipse, mappingId));
    uid id = mappingManager->addMapping(mapping);

    // Add it to the GUI.
    addMappingItem(mappingId);

    // Return the id.
    return id;
  }
}

uid MainWindow::createQuadColorMapping(uid mappingId,
                                       uid paintId,
                                       const QVector<QPointF> &dst)
{
  // Cannot create element with already existing id or element for which no paint exists.
  if (Mapping::getUidAllocator().exists(mappingId) ||
      !Paint::getUidAllocator().exists(paintId) ||
      paintId == NULL_UID)
    return NULL_UID;

  else
  {
    Paint::ptr paint = mappingManager->getPaintById(paintId);
    Q_ASSERT(dst.size() == 4);

    MShape::ptr outputQuad(new Quad(dst[0], dst[1], dst[2], dst[3]));

    // Add it to the manager.
    Mapping::ptr mapping(new ColorMapping(paint, outputQuad, mappingId));
    uid id = mappingManager->addMapping(mapping);

    // Add it to the GUI.
    addMappingItem(mappingId);

    // Return the id.
    return id;
  }
}

uid MainWindow::createTriangleColorMapping(uid mappingId,
                                           uid paintId,
                                           const QVector<QPointF> &dst)
{
  // Cannot create element with already existing id or element for which no paint exists.
  if (Mapping::getUidAllocator().exists(mappingId) ||
      !Paint::getUidAllocator().exists(paintId) ||
      paintId == NULL_UID)
    return NULL_UID;

  else
  {
    Paint::ptr paint = mappingManager->getPaintById(paintId);
    Q_ASSERT(dst.size() == 3);

    MShape::ptr outputTriangle(new Triangle(dst[0], dst[1], dst[2]));

    // Add it to the manager.
    Mapping::ptr mapping(new ColorMapping(paint, outputTriangle, mappingId));
    uid id = mappingManager->addMapping(mapping);

    // Add it to the GUI.
    addMappingItem(mappingId);

    // Return the id.
    return id;
  }
}

uid MainWindow::createEllipseColorMapping(uid mappingId,
                                          uid paintId,
                                          const QVector<QPointF> &dst)
{
  // Cannot create element with already existing id or element for which no paint exists.
  if (Mapping::getUidAllocator().exists(mappingId) ||
      !Paint::getUidAllocator().exists(paintId) ||
      paintId == NULL_UID)
    return NULL_UID;

  else
  {
    Paint::ptr paint = mappingManager->getPaintById(paintId);
    Q_ASSERT(dst.size() == 4);

    MShape::ptr outputEllipse(new Ellipse(dst[0], dst[1], dst[2], dst[3]));

    // Add it to the manager.
    Mapping::ptr mapping(new ColorMapping(paint, outputEllipse, mappingId));
    uid id = mappingManager->addMapping(mapping);

    // Add it to the GUI.
    addMappingItem(mappingId);

    // Return the id.
    return id;
  }
}


void MainWindow::setMappingVisible(uid mappingId, bool visible)
{
  // Set mapping visibility
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);

  if (mapping.isNull())
  {
    qDebug() << "No such mapping id" << endl;
  }
  else
  {
    mapping->setVisible(visible);
    // Change list item check state
    QModelIndex index = mappingListModel->getIndexFromId(mappingId);
    mappingListModel->setData(index, visible, Qt::CheckStateRole);
    // Update canvases.
    updateCanvases();
  }
}

void MainWindow::setMappingSolo(uid mappingId, bool solo)
{
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);
  if (!mapping.isNull()) {
    // Turn this mapping into solo mode
    mapping->setSolo(solo);
    // Change list item check state
    QModelIndex index = mappingListModel->getIndexFromId(mappingId);
    mappingListModel->setData(index, solo, Qt::CheckStateRole + 1);
    // Update canvases
    updateCanvases();
  }
}

void MainWindow::setMappingLocked(uid mappingId, bool locked)
{
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);

  if (!mapping.isNull()) {
    // Lock position of mapping
    mapping->setLocked(locked);
    // Lock shape too.
    mapping->getShape()->setLocked(locked);
    // Change list item check state
    QModelIndex index = mappingListModel->getIndexFromId(mappingId);
    mappingListModel->setData(index, locked, Qt::CheckStateRole + 2);
    // Update canvases
    updateCanvases();
  }
}

void MainWindow::deleteMapping(uid mappingId)
{
  // Cannot delete unexisting mapping.
  if (Mapping::getUidAllocator().exists(mappingId))
  {
    removeMappingItem(mappingId);
  }
}

void MainWindow::duplicateMapping(uid mappingId)
{
  // Current Mapping
  Mapping::ptr mappingPtr = mappingManager->getMappingById(mappingId);

  // Get Mapping Paint and Shape
  Paint::ptr paint = mappingPtr->getPaint();
  MShape::ptr shape = mappingPtr->getShape();
  // Temporary shared pointers
  MShape::ptr shapePtr;
  // Create new mapping
  Mapping *mapping;

  QString shapeType = shape->getType();

  // Code below need to be improved it's feel like duplicated
  if (paint->getType() == "color") // Color paint
  {
    if (shapeType == "quad")
      shapePtr = MShape::ptr(new Quad(shape->getVertex(0), shape->getVertex(1),
                                      shape->getVertex(2), shape->getVertex(3)));

    if (shapeType == "triangle")
      shapePtr = MShape::ptr(new Triangle(shape->getVertex(0), shape->getVertex(1), shape->getVertex(2)));

    if (shapeType == "ellipse")
      shapePtr = MShape::ptr(new Ellipse(shape->getVertex(0), shape->getVertex(1), shape->getVertex(2),
                                         shape->getVertex(3)));

    mapping = new ColorMapping(paint, shapePtr);
  }
  else // Or Texture Paint
  {
    MShape::ptr inputShape = mappingPtr->getInputShape();

    if (shapeType == "mesh")
      shapePtr = MShape::ptr(new Mesh(shape->getVertex(0), shape->getVertex(1),
                                      shape->getVertex(3), shape->getVertex(2)));

    if (shapeType == "triangle")
      shapePtr = MShape::ptr(new Triangle(shape->getVertex(0), shape->getVertex(1), shape->getVertex(2)));

    if (shapeType == "ellipse")
      shapePtr = MShape::ptr(new Ellipse(shape->getVertex(0), shape->getVertex(1), shape->getVertex(2),
                                         shape->getVertex(3), shape->getVertex(4)));

    mapping = new TextureMapping(paint, shapePtr, inputShape);
  }

  // Scaling of duplicated mapping
  if (shapeType == "quad" || shapeType == "mesh")
    shapePtr->translate(QPointF(20, 20));
  else
    shapePtr->translate(QPointF(0, 20));

  // Create new duplicated mapping item
  Mapping::ptr clonedMapping(mapping);
  uint cloneId = mappingManager->addMapping(clonedMapping);
  addMappingItem(cloneId);
}

/// Deletes/removes a paint and all associated mappigns.
void MainWindow::deletePaint(uid paintId, bool replace)
{
  // Cannot delete unexisting paint.
  if (Paint::getUidAllocator().exists(paintId))
  {
    if (replace == false) {
      int r = QMessageBox::warning(this, tr("MapMap"),
                                   tr("Remove this paint and all its associated mappings?"),
                                   QMessageBox::Ok | QMessageBox::Cancel);
      if (r == QMessageBox::Ok)
      {
        removePaintItem(paintId);
      }
    }
    else
      removePaintItem(paintId);
  }
}

void MainWindow::windowModified()
{
  setWindowModified(true);
  updateStatusBar();
}

void MainWindow::createLayout()
{
  // Create paint list.
  paintList = new QListWidget;
  paintList->setSelectionMode(QAbstractItemView::SingleSelection);
  paintList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  paintList->setDefaultDropAction(Qt::MoveAction);
  paintList->setDragDropMode(QAbstractItemView::InternalMove);
  paintList->setMinimumWidth(PAINT_LIST_MINIMUM_HEIGHT);

  // Create paint panel.
  paintPropertyPanel = new QStackedWidget;
  paintPropertyPanel->setDisabled(true);
  paintPropertyPanel->setMinimumHeight(PAINT_PROPERTY_PANEL_MINIMUM_HEIGHT);

  // Create mapping list.
  mappingList = new QTableView;
  mappingList->setSelectionMode(QAbstractItemView::SingleSelection);
  mappingList->setSelectionBehavior(QAbstractItemView::SelectRows);
  mappingList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  mappingList->setDragEnabled(true);
  mappingList->setAcceptDrops(true);
  mappingList->setDropIndicatorShown(true);
  mappingList->setEditTriggers(QAbstractItemView::DoubleClicked);
  mappingList->setMinimumHeight(MAPPING_LIST_MINIMUM_HEIGHT);
  mappingList->setContentsMargins(0, 0, 0, 0);
  // Set view delegate
  mappingListModel = new MappingListModel;
  mappingItemDelegate = new MappingItemDelegate;
  mappingList->setModel(mappingListModel);
  mappingList->setItemDelegate(mappingItemDelegate);
  // Pimp Mapping table widget
  mappingList->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
  mappingList->verticalHeader()->setResizeMode(QHeaderView::ResizeToContents);
  mappingList->horizontalHeader()->setStretchLastSection(true);
  mappingList->setShowGrid(false);
  mappingList->horizontalHeader()->hide();
  mappingList->verticalHeader()->hide();
  mappingList->setMouseTracking(true);// Important

  // Create property panel.
  mappingPropertyPanel = new QStackedWidget;
  mappingPropertyPanel->setDisabled(true);
  mappingPropertyPanel->setMinimumHeight(MAPPING_PROPERTY_PANEL_MINIMUM_HEIGHT);

  // Create canvases.
  sourceCanvas = new MapperGLCanvas(this, false);
  sourceCanvas->setFocusPolicy(Qt::ClickFocus);
  sourceCanvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  sourceCanvas->setMinimumSize(CANVAS_MINIMUM_WIDTH, CANVAS_MINIMUM_HEIGHT);

  sourceCanvasToolbar = new MapperGLCanvasToolbar(sourceCanvas, this);
  QVBoxLayout* sourceLayout = new QVBoxLayout;
  sourcePanel = new QWidget(this);

  sourceLayout->setContentsMargins(0, 0, 0, 0);
  sourceLayout->addWidget(sourceCanvas);
  sourceLayout->addWidget(sourceCanvasToolbar, 0, Qt::AlignRight);
  sourcePanel->setLayout(sourceLayout);

  destinationCanvas = new MapperGLCanvas(this, true, 0, (QGLWidget*)sourceCanvas->viewport());
  destinationCanvas->setFocusPolicy(Qt::ClickFocus);
  destinationCanvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  destinationCanvas->setMinimumSize(CANVAS_MINIMUM_WIDTH, CANVAS_MINIMUM_HEIGHT);

  destinationCanvasToolbar = new MapperGLCanvasToolbar(destinationCanvas, this);
  QVBoxLayout* destinationLayout = new QVBoxLayout;
  destinationPanel = new QWidget(this);

  destinationLayout->setContentsMargins(0, 0, 0, 0);
  destinationLayout->addWidget(destinationCanvas);
  destinationLayout->addWidget(destinationCanvasToolbar, 0, Qt::AlignRight);
  destinationPanel->setLayout(destinationLayout);

  outputWindow = new OutputGLWindow(this, destinationCanvas);
  outputWindow->installEventFilter(destinationCanvas);

  // Source scene changed -> change destination.
  connect(sourceCanvas->scene(), SIGNAL(changed(const QList<QRectF>&)),
          destinationCanvas,     SLOT(update()));

  // Destination scene changed -> change output window.
  connect(destinationCanvas->scene(), SIGNAL(changed(const QList<QRectF>&)),
          outputWindow->getCanvas(),  SLOT(update()));

  // Output changed -> change destinatioin
  // XXX si je decommente cette ligne alors quand je clique sur ajouter media ca gele...
  //  connect(outputWindow->getCanvas()->scene(), SIGNAL(changed(const QList<QRectF>&)),
  //          destinationCanvas,                  SLOT(updateCanvas()));

  // Create console logging output
  consoleWindow = ConsoleWindow::console();
  consoleWindow->setVisible(false);

  // Create layout.
  paintSplitter = new QSplitter(Qt::Vertical);
  paintSplitter->addWidget(paintList);
  paintSplitter->addWidget(paintPropertyPanel);

  mappingSplitter = new QSplitter(Qt::Vertical);
  mappingSplitter->addWidget(mappingList);
  mappingSplitter->addWidget(mappingPropertyPanel);

  // Content tab.
  contentTab = new QTabWidget;
  contentTab->addTab(paintSplitter, QIcon(":/add-video"), tr("Paints"));
  contentTab->addTab(mappingSplitter, QIcon(":/add-mesh"), tr("Mappings"));

  canvasSplitter = new QSplitter(Qt::Vertical);
  canvasSplitter->addWidget(sourcePanel);
  canvasSplitter->addWidget(destinationPanel);

  mainSplitter = new QSplitter(Qt::Horizontal);
  mainSplitter->addWidget(canvasSplitter);
  mainSplitter->addWidget(contentTab);

  // Initialize size to 9:1 proportions.
  QSize sz = mainSplitter->size();
  QList<int> sizes;
  sizes.append(sz.width() * 0.9);
  sizes.append(sz.width() - sizes.at(0));
  mainSplitter->setSizes(sizes);

  // Upon resizing window, give some extra stretch expansion to canvasSplitter.
  mainSplitter->setStretchFactor(0, 1);

  // Final setups.
  setWindowTitle(tr("MapMap"));
  resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
  setCentralWidget(mainSplitter);

  // Connect mapping and paint lists signals and slots.
  connectProjectWidgets();

  // Reset focus on main window.
  setFocus();
}

void MainWindow::createActions()
{
  // New.
  newAction = new QAction(tr("&New"), this);
  newAction->setIcon(QIcon(":/new"));
  newAction->setShortcut(QKeySequence::New);
  newAction->setToolTip(tr("Create a new project"));
  newAction->setIconVisibleInMenu(false);
  newAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(newAction);
  connect(newAction, SIGNAL(triggered()), this, SLOT(newFile()));

  // Open.
  openAction = new QAction(tr("&Open..."), this);
  openAction->setIcon(QIcon(":/open"));
  openAction->setShortcut(QKeySequence::Open);
  openAction->setToolTip(tr("Open an existing project"));
  openAction->setIconVisibleInMenu(false);
  openAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(openAction);
  connect(openAction, SIGNAL(triggered()), this, SLOT(open()));

  // Save.
  saveAction = new QAction(tr("&Save"), this);
  saveAction->setIcon(QIcon(":/save"));
  saveAction->setShortcut(QKeySequence::Save);
  saveAction->setToolTip(tr("Save the project"));
  saveAction->setIconVisibleInMenu(false);
  saveAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(saveAction);
  connect(saveAction, SIGNAL(triggered()), this, SLOT(save()));

  // Save as.
  saveAsAction = new QAction(tr("Save &As..."), this);
  saveAsAction->setIcon(QIcon(":/save-as"));
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  saveAsAction->setToolTip(tr("Save the project as..."));
  saveAsAction->setIconVisibleInMenu(false);
  saveAsAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(saveAsAction);
  connect(saveAsAction, SIGNAL(triggered()), this, SLOT(saveAs()));

  // Recents file
  for (int i = 0; i < MaxRecentFiles; i++)
  {
    recentFileActions[i] = new QAction(this);
    recentFileActions[i]->setVisible(false);
    connect(recentFileActions[i], SIGNAL(triggered()),
            this, SLOT(openRecentFile()));
  }

  // Recent video
  for (int i = 0; i < MaxRecentVideo; i++)
  {
    recentVideoActions[i] = new QAction(this);
    recentVideoActions[i]->setVisible(false);
    connect(recentVideoActions[i], SIGNAL(triggered()), this, SLOT(openRecentVideo()));
  }

  // Clear recent video list action
  clearRecentFileActions = new QAction(this);
  clearRecentFileActions->setVisible(true);
  connect(clearRecentFileActions, SIGNAL(triggered()), this, SLOT(clearRecentFileList()));

  // Empty list of recent video action
  emptyRecentVideos = new QAction(tr("No Recents Videos"), this);
  emptyRecentVideos->setEnabled(false);


  // Import Media.
  importMediaAction = new QAction(tr("&Import Media File..."), this);
  importMediaAction->setShortcut(Qt::CTRL + Qt::Key_I);
  importMediaAction->setIcon(QIcon(":/add-video"));
  importMediaAction->setToolTip(tr("Import a video or image file..."));
  importMediaAction->setIconVisibleInMenu(false);
  importMediaAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(importMediaAction);
  connect(importMediaAction, SIGNAL(triggered()), this, SLOT(importMedia()));

  // Add color.
  addColorAction = new QAction(tr("Add &Color Paint..."), this);
  addColorAction->setShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_A);
  addColorAction->setIcon(QIcon(":/add-color"));
  addColorAction->setToolTip(tr("Add a color paint..."));
  addColorAction->setIconVisibleInMenu(false);
  addColorAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(addColorAction);
  connect(addColorAction, SIGNAL(triggered()), this, SLOT(addColor()));

  // Exit/quit.
  exitAction = new QAction(tr("E&xit"), this);
  exitAction->setShortcut(QKeySequence::Quit);
  exitAction->setToolTip(tr("Exit the application"));
  exitAction->setIconVisibleInMenu(false);
  exitAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(exitAction);
  connect(exitAction, SIGNAL(triggered()), this, SLOT(close()));

  // Undo action
  undoAction = undoStack->createUndoAction(this, tr("&Undo"));
  undoAction->setShortcut(QKeySequence::Undo);
  undoAction->setIconVisibleInMenu(false);
  undoAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(undoAction);

  //Redo action
  redoAction = undoStack->createRedoAction(this, tr("&Redo"));
  redoAction->setShortcut(QKeySequence::Redo);
  redoAction->setIconVisibleInMenu(false);
  redoAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(redoAction);

  // About.
  aboutAction = new QAction(tr("&About"), this);
  aboutAction->setToolTip(tr("Show the application's About box"));
  aboutAction->setIconVisibleInMenu(false);
  aboutAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(aboutAction);
  connect(aboutAction, SIGNAL(triggered()), this, SLOT(about()));

  // Duplicate.
  cloneMappingAction = new QAction(tr("Duplicate"), this);
  cloneMappingAction->setShortcut(Qt::CTRL + Qt::Key_D);
  cloneMappingAction->setToolTip(tr("Duplicate item"));
  cloneMappingAction->setIconVisibleInMenu(false);
  cloneMappingAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(cloneMappingAction);
  connect(cloneMappingAction, SIGNAL(triggered()), this, SLOT(duplicateMappingItem()));

  // Delete mapping.
  deleteMappingAction = new QAction(tr("Delete mapping"), this);
  deleteMappingAction->setShortcut(QKeySequence::Delete);
  deleteMappingAction->setToolTip(tr("Delete item"));
  deleteMappingAction->setIconVisibleInMenu(false);
  deleteMappingAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(deleteMappingAction);
  connect(deleteMappingAction, SIGNAL(triggered()), this, SLOT(deleteMappingItem()));

  // Rename mapping.
  renameMappingAction = new QAction(tr("Rename"), this);
  renameMappingAction->setShortcut(Qt::Key_F2);
  renameMappingAction->setToolTip(tr("Rename item"));
  renameMappingAction->setIconVisibleInMenu(false);
  renameMappingAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(renameMappingAction);
  connect(renameMappingAction, SIGNAL(triggered()), this, SLOT(renameMappingItem()));

  // Lock mapping.
  mappingLockedAction = new QAction(tr("Lock mapping"), this);
  mappingLockedAction->setToolTip(tr("Lock mapping item"));
  mappingLockedAction->setIconVisibleInMenu(false);
  mappingLockedAction->setCheckable(true);
  mappingLockedAction->setChecked(false);
  mappingLockedAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(mappingLockedAction);
  connect(mappingLockedAction, SIGNAL(triggered(bool)), this, SLOT(setMappingItemLocked(bool)));

  // Hide mapping.
  mappingHideAction = new QAction(tr("Hide mapping"), this);
  mappingHideAction->setToolTip(tr("Hide mapping item"));
  mappingHideAction->setIconVisibleInMenu(false);
  mappingHideAction->setCheckable(true);
  mappingHideAction->setChecked(false);
  mappingHideAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(mappingHideAction);
  connect(mappingHideAction, SIGNAL(triggered(bool)), this, SLOT(setMappingItemHide(bool)));

  // Solo mapping.
  mappingSoloAction = new QAction(tr("Solo mapping"), this);
  mappingSoloAction->setToolTip(tr("solo mapping item"));
  mappingSoloAction->setIconVisibleInMenu(false);
  mappingSoloAction->setCheckable(true);
  mappingSoloAction->setChecked(false);
  mappingSoloAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(mappingSoloAction);
  connect(mappingSoloAction, SIGNAL(triggered(bool)), this, SLOT(setMappingItemSolo(bool)));

  // Delete paint.
  deletePaintAction = new QAction(tr("Delete paint"), this);
  //deletePaintAction->setShortcut(tr("CTRL+DEL"));
  deletePaintAction->setToolTip(tr("Delete item"));
  deletePaintAction->setIconVisibleInMenu(false);
  deletePaintAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(deletePaintAction);
  connect(deletePaintAction, SIGNAL(triggered()), this, SLOT(deletePaintItem()));

  // Rename paint.
  renamePaintAction = new QAction(tr("Rename"), this);
  //renamePaintAction->setShortcut(Qt::Key_F2);
  renamePaintAction->setToolTip(tr("Rename item"));
  renamePaintAction->setIconVisibleInMenu(false);
  renamePaintAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(renamePaintAction);
  connect(renamePaintAction, SIGNAL(triggered()), this, SLOT(renamePaintItem()));

  // Preferences...
  preferencesAction = new QAction(tr("&Preferences..."), this);
  //preferencesAction->setIcon(QIcon(":/preferences"));
  preferencesAction->setShortcut(Qt::CTRL + Qt::Key_Comma);
  preferencesAction->setToolTip(tr("Configure preferences..."));
  //preferencesAction->setIconVisibleInMenu(false);
  preferencesAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(preferencesAction);
  connect(preferencesAction, SIGNAL(triggered()), this, SLOT(preferences()));

  // Add quad/mesh.
  addMeshAction = new QAction(tr("Add Quad/&Mesh"), this);
  addMeshAction->setShortcut(Qt::CTRL + Qt::Key_M);
  addMeshAction->setIcon(QIcon(":/add-mesh"));
  addMeshAction->setToolTip(tr("Add quad/mesh"));
  addMeshAction->setIconVisibleInMenu(false);
  addMeshAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(addMeshAction);
  connect(addMeshAction, SIGNAL(triggered()), this, SLOT(addMesh()));
  addMeshAction->setEnabled(false);

  // Add triangle.
  addTriangleAction = new QAction(tr("Add &Triangle"), this);
  addTriangleAction->setShortcut(Qt::CTRL + Qt::Key_T);
  addTriangleAction->setIcon(QIcon(":/add-triangle"));
  addTriangleAction->setToolTip(tr("Add triangle"));
  addTriangleAction->setIconVisibleInMenu(false);
  addTriangleAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(addTriangleAction);
  connect(addTriangleAction, SIGNAL(triggered()), this, SLOT(addTriangle()));
  addTriangleAction->setEnabled(false);

  // Add ellipse.
  addEllipseAction = new QAction(tr("Add &Ellipse"), this);
  addEllipseAction->setShortcut(Qt::CTRL + Qt::Key_E);
  addEllipseAction->setIcon(QIcon(":/add-ellipse"));
  addEllipseAction->setToolTip(tr("Add ellipse"));
  addEllipseAction->setIconVisibleInMenu(false);
  addEllipseAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(addEllipseAction);
  connect(addEllipseAction, SIGNAL(triggered()), this, SLOT(addEllipse()));
  addEllipseAction->setEnabled(false);

  // Play.
  playAction = new QAction(tr("Play"), this);
  playAction->setShortcut(Qt::Key_Space);
  playAction->setIcon(QIcon(":/play"));
  playAction->setToolTip(tr("Play"));
  playAction->setIconVisibleInMenu(false);
  playAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(playAction);
  connect(playAction, SIGNAL(triggered()), this, SLOT(play()));
  playAction->setVisible(true);

  // Pause.
  pauseAction = new QAction(tr("Pause"), this);
  pauseAction->setShortcut(Qt::Key_Space);
  pauseAction->setIcon(QIcon(":/pause"));
  pauseAction->setToolTip(tr("Pause"));
  pauseAction->setIconVisibleInMenu(false);
  pauseAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(pauseAction);
  connect(pauseAction, SIGNAL(triggered()), this, SLOT(pause()));
  pauseAction->setVisible(false);

  // Rewind.
  rewindAction = new QAction(tr("Rewind"), this);
  rewindAction->setShortcut(Qt::CTRL + Qt::Key_R);
  rewindAction->setIcon(QIcon(":/rewind"));
  rewindAction->setToolTip(tr("Rewind"));
  rewindAction->setIconVisibleInMenu(false);
  rewindAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(rewindAction);
  connect(rewindAction, SIGNAL(triggered()), this, SLOT(rewind()));

  // Toggle display of output window.
  outputFullScreenAction = new QAction(tr("&Full Screen"), this);
  outputFullScreenAction->setShortcut(Qt::CTRL + Qt::Key_F);
  outputFullScreenAction->setIcon(QIcon(":/fullscreen"));
  outputFullScreenAction->setToolTip(tr("Full screen mode"));
  outputFullScreenAction->setIconVisibleInMenu(false);
  outputFullScreenAction->setCheckable(true);
  // Don't be displayed by default
  outputFullScreenAction->setChecked(false);
  outputFullScreenAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(outputFullScreenAction);
  // Manage fullscreen/modal show of GL output window.
  connect(outputFullScreenAction, SIGNAL(toggled(bool)), outputWindow, SLOT(setFullScreen(bool)));
  // When closing the GL output window or hit ESC key, uncheck the action in menu.
//  connect(outputWindow, SIGNAL(closed()), outputFullScreenAction, SLOT(toggle()));
  connect(QApplication::desktop(), SIGNAL(screenCountChanged(int)), outputWindow, SLOT(updateScreenCount(int)));
  // Create hiden action for closing output window
  QAction *closeOutput = new QAction(tr("Close output"), this);
  closeOutput->setShortcut(Qt::Key_Escape);
  closeOutput->setShortcutContext(Qt::ApplicationShortcut);
  addAction(closeOutput);
  connect(closeOutput, SIGNAL(triggered(bool)), this, SLOT(exitFullScreen()));

  // Toggle display of canvas controls.
  displayControlsAction = new QAction(tr("&Display Canvas Controls"), this);
  displayControlsAction->setShortcut(Qt::ALT + Qt::Key_C);
  displayControlsAction->setIcon(QIcon(":/control-points"));
  displayControlsAction->setToolTip(tr("Display canvas controls"));
  displayControlsAction->setIconVisibleInMenu(false);
  displayControlsAction->setCheckable(true);
  displayControlsAction->setChecked(_displayControls);
  displayControlsAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(displayControlsAction);
  // Manage show/hide of canvas controls.
  connect(displayControlsAction, SIGNAL(toggled(bool)), this, SLOT(enableDisplayControls(bool)));
  connect(displayControlsAction, SIGNAL(toggled(bool)), outputWindow, SLOT(setDisplayCrosshair(bool)));

  // Toggle sticky vertices
  stickyVerticesAction = new QAction(tr("&Sticky Vertices"), this);
  stickyVerticesAction->setShortcut(Qt::ALT + Qt::Key_S);
  stickyVerticesAction->setIcon(QIcon(":/control-points"));
  stickyVerticesAction->setToolTip(tr("Enable sticky vertices"));
  stickyVerticesAction->setIconVisibleInMenu(false);
  stickyVerticesAction->setCheckable(true);
  stickyVerticesAction->setChecked(_stickyVertices);
  stickyVerticesAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(stickyVerticesAction);
  // Manage sticky vertices
  connect(stickyVerticesAction, SIGNAL(toggled(bool)), this, SLOT(enableStickyVertices(bool)));

  displayTestSignalAction = new QAction(tr("&Display Test Signal"), this);
  displayTestSignalAction->setShortcut(Qt::ALT + Qt::Key_T);
  displayTestSignalAction->setIcon(QIcon(":/control-points"));
  displayTestSignalAction->setToolTip(tr("Display test signal"));
  displayTestSignalAction->setIconVisibleInMenu(false);
  displayTestSignalAction->setCheckable(true);
  displayTestSignalAction->setChecked(false);
  displayTestSignalAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(displayTestSignalAction);
  // Manage show/hide of test signal
  connect(displayTestSignalAction, SIGNAL(toggled(bool)), outputWindow, SLOT(setDisplayTestSignal(bool)));
//  connect(displayTestSignalAction, SIGNAL(toggled(bool)), this, SLOT(update()));

  // Toggle display of Undo Stack
  displayUndoStackAction = new QAction(tr("Display &Undo Stack"), this);
  displayUndoStackAction->setShortcut(Qt::ALT + Qt::Key_U);
  displayUndoStackAction->setCheckable(true);
  displayUndoStackAction->setChecked(_displayUndoStack);
  displayUndoStackAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(displayUndoStackAction);
  // Manage show/hide of Undo Stack
  connect(displayUndoStackAction, SIGNAL(toggled(bool)), this, SLOT(displayUndoStack(bool)));

  // Toggle display of Console output
  openConsoleAction = new QAction(tr("Open Conso&le"), this);
  openConsoleAction->setShortcut(Qt::ALT + Qt::Key_L);
  openConsoleAction->setCheckable(true);
  openConsoleAction->setChecked(false);
  openConsoleAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(openConsoleAction);
  connect(openConsoleAction, SIGNAL(toggled(bool)), consoleWindow, SLOT(setVisible(bool)));
  // uncheck action when window is closed
  connect(consoleWindow, SIGNAL(windowClosed()), openConsoleAction, SLOT(toggle()));

  // Toggle display of zoom tool buttons
  displayZoomToolAction = new QAction(tr("Display &Zoom Toolbar"), this);
  displayZoomToolAction->setShortcut(Qt::ALT + Qt::Key_Z);
  displayZoomToolAction->setCheckable(true);
  displayZoomToolAction->setChecked(true);
  displayZoomToolAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(displayZoomToolAction);
  connect(displayZoomToolAction, SIGNAL(toggled(bool)), sourceCanvasToolbar, SLOT(showZoomToolBar(bool)));
  connect(displayZoomToolAction, SIGNAL(toggled(bool)), destinationCanvasToolbar, SLOT(showZoomToolBar(bool)));

  // Toggle show/hide menuBar
  showMenuBarAction = new QAction(tr("&Menu Bar"), this);
  showMenuBarAction->setCheckable(true);
  showMenuBarAction->setChecked(_showMenuBar);
  connect(showMenuBarAction, SIGNAL(toggled(bool)), this, SLOT(showMenuBar(bool)));

  // Perspectives
  // Main perspective (Source + destination)
  mainViewAction = new QAction(tr("Main Perspective"), this);
  mainViewAction->setCheckable(true);
  mainViewAction->setChecked(true);
  mainViewAction->setShortcut(Qt::CTRL + Qt::ALT + Qt::Key_1);
  mainViewAction->setToolTip(tr("Switch to the Main perspective."));
  connect(mainViewAction, SIGNAL(triggered(bool)), canvasSplitter->widget(0), SLOT(setVisible(bool)));
  connect(mainViewAction, SIGNAL(triggered(bool)), canvasSplitter->widget(1), SLOT(setVisible(bool)));
  // Source Only
  sourceViewAction = new QAction(tr("Source Perspective"), this);
  sourceViewAction->setCheckable(true);
  sourceViewAction->setShortcut(Qt::CTRL + Qt::ALT + Qt::Key_2);
  sourceViewAction->setToolTip(tr("Switch to the Source perspective."));
  connect(sourceViewAction, SIGNAL(triggered(bool)), canvasSplitter->widget(0), SLOT(setVisible(bool)));
  connect(sourceViewAction, SIGNAL(triggered(bool)), canvasSplitter->widget(1), SLOT(setHidden(bool)));
  // Destination Only
  destViewAction = new QAction(tr("Destination Perspective"), this);
  destViewAction->setCheckable(true);
  destViewAction->setShortcut(Qt::CTRL + Qt::ALT + Qt::Key_3);
  destViewAction->setToolTip(tr("Switch to the Destination perspective."));
  connect(destViewAction, SIGNAL(triggered(bool)), canvasSplitter->widget(0), SLOT(setHidden(bool)));
  connect(destViewAction, SIGNAL(triggered(bool)), canvasSplitter->widget(1), SLOT(setVisible(bool)));
  // Groups all actions
  perspectiveActionGroup = new QActionGroup(this);
  perspectiveActionGroup->addAction(mainViewAction);
  perspectiveActionGroup->addAction(sourceViewAction);
  perspectiveActionGroup->addAction(destViewAction);
}

void MainWindow::startFullScreen()
{
  // Remove canvas controls.
  displayControlsAction->setChecked(false);
  // Display output window.
  outputFullScreenAction->setChecked(true);
}

void MainWindow::createMenus()
{
  QMenuBar *menuBar = NULL;

#ifdef __MACOSX_CORE__
  menuBar = new QMenuBar(0);
  //this->setMenuBar(menuBar);
#else
  menuBar = this->menuBar();
#endif

  // File
  fileMenu = menuBar->addMenu(tr("&File"));
  fileMenu->addAction(newAction);
  fileMenu->addAction(openAction);
  fileMenu->addAction(saveAction);
  fileMenu->addAction(saveAsAction);
  fileMenu->addSeparator();
  fileMenu->addAction(importMediaAction);
  fileMenu->addAction(addColorAction);

  // Recent file separator
  separatorAction = fileMenu->addSeparator();
  recentFileMenu = fileMenu->addMenu(tr("Open Recents Projects"));
  for (int i = 0; i < MaxRecentFiles; ++i)
    recentFileMenu->addAction(recentFileActions[i]);
  recentFileMenu->addAction(clearRecentFileActions);

  // Recent import video
  recentVideoMenu = fileMenu->addMenu(tr("Open Recents Videos"));
  recentVideoMenu->addAction(emptyRecentVideos);
  for (int i = 0; i < MaxRecentVideo; ++i)
    recentVideoMenu->addAction(recentVideoActions[i]);

  // Exit
  fileMenu->addSeparator();
  fileMenu->addAction(exitAction);


  // Edit.
  editMenu = menuBar->addMenu(tr("&Edit"));
  // Undo & Redo menu
  editMenu->addAction(undoAction);
  editMenu->addAction(redoAction);
  editMenu->addSeparator();
  // Source canvas menu
  sourceMenu = editMenu->addMenu(tr("&Source"));
  sourceMenu->setEnabled(false);
  sourceMenu->addAction(deletePaintAction);
  sourceMenu->addAction(renamePaintAction);
  // Destination canvas menu
  destinationMenu = editMenu->addMenu(tr("&Destination"));
  destinationMenu->setEnabled(false);
  destinationMenu->addAction(cloneMappingAction);
  destinationMenu->addAction(deleteMappingAction);
  destinationMenu->addAction(renameMappingAction);
  editMenu->addSeparator();
  // Preferences
  editMenu->addAction(preferencesAction);

  // View.
  viewMenu = menuBar->addMenu(tr("&View"));
  // Toolbars menu
  toolBarsMenu = viewMenu->addMenu(tr("Toolbars"));
#ifdef Q_OS_LINUX
  if (QString(getenv("XDG_CURRENT_DESKTOP")).toLower() != "unity")
    toolBarsMenu->addAction(showMenuBarAction);
#endif
#ifdef Q_OS_WIN
  toolBarsMenu->addAction(showMenuBarAction);
#endif
  viewMenu->addSeparator();
  viewMenu->addAction(displayControlsAction);
  viewMenu->addAction(stickyVerticesAction);
  viewMenu->addAction(displayTestSignalAction);
  viewMenu->addSeparator();
  viewMenu->addAction(displayUndoStackAction);
  viewMenu->addAction(displayZoomToolAction);
  viewMenu->addSeparator();
  viewMenu->addAction(outputFullScreenAction);

  // Run.
  playbackMenu = menuBar->addMenu(tr("&Playback"));
  playbackMenu->addAction(playAction);
  playbackMenu->addAction(pauseAction);
  playbackMenu->addAction(rewindAction);

  // Tools
  toolsMenu = menuBar->addMenu(tr("&Tools"));
  toolsMenu->addAction(openConsoleAction);

  // Window
  windowMenu = menuBar->addMenu(tr("&Window"));
  windowMenu->addAction(mainViewAction);
  windowMenu->addAction(sourceViewAction);
  windowMenu->addAction(destViewAction);

  // Help.
  helpMenu = menuBar->addMenu(tr("&Help"));
  helpMenu->addAction(aboutAction);
  //  helpMenu->addAction(aboutQtAction);

}

void MainWindow::createMappingContextMenu()
{
  // Context menu.
  mappingContextMenu = new QMenu(this);

  // Add different Action
  mappingContextMenu->addAction(cloneMappingAction);
  mappingContextMenu->addAction(deleteMappingAction);
  mappingContextMenu->addAction(renameMappingAction);
  mappingContextMenu->addAction(mappingLockedAction);
  mappingContextMenu->addAction(mappingHideAction);
  mappingContextMenu->addAction(mappingSoloAction);

  // Set context menu policy
  mappingList->setContextMenuPolicy(Qt::CustomContextMenu);
  destinationCanvas->setContextMenuPolicy(Qt::CustomContextMenu);
  outputWindow->setContextMenuPolicy(Qt::CustomContextMenu);

  // Context Menu Connexions
  connect(mappingItemDelegate, SIGNAL(itemContextMenuRequested(const QPoint&)),
          this, SLOT(showMappingContextMenu(const QPoint&)), Qt::QueuedConnection);
  connect(destinationCanvas, SIGNAL(shapeContextMenuRequested(const QPoint&)), this, SLOT(showMappingContextMenu(const QPoint&)));
  connect(outputWindow->getCanvas(), SIGNAL(shapeContextMenuRequested(const QPoint&)), this, SLOT(showMappingContextMenu(const QPoint&)));
}

void MainWindow::createPaintContextMenu()
{
  // Paint Context Menu
  paintContextMenu = new QMenu(this);

  // Add Actions
  paintContextMenu->addAction(deletePaintAction);
  paintContextMenu->addAction(renamePaintAction);

  // Define Context policy
  paintList->setContextMenuPolicy(Qt::CustomContextMenu);
  sourceCanvas->setContextMenuPolicy(Qt::CustomContextMenu);

  // Connexions
  connect(paintList, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showPaintContextMenu(const QPoint&)));
  connect(sourceCanvas, SIGNAL(shapeContextMenuRequested(const QPoint&)), this, SLOT(showPaintContextMenu(const QPoint&)));
}

void MainWindow::createToolBars()
{
  mainToolBar = addToolBar(tr("&Toolbar"));
  mainToolBar->setIconSize(QSize(MM::TOP_TOOLBAR_ICON_SIZE, MM::TOP_TOOLBAR_ICON_SIZE));
  mainToolBar->setMovable(false);
  mainToolBar->addAction(importMediaAction);
  mainToolBar->addAction(addColorAction);

  mainToolBar->addSeparator();

  mainToolBar->addAction(addMeshAction);
  mainToolBar->addAction(addTriangleAction);
  mainToolBar->addAction(addEllipseAction);

  mainToolBar->addSeparator();

  mainToolBar->addAction(outputFullScreenAction);
  mainToolBar->addAction(displayTestSignalAction);

  // XXX: style hack: dummy expanding widget allows the placement of toolbar at the top right
  // From: http://www.qtcentre.org/threads/9102-QToolbar-setContentsMargins
  QWidget* spacer = new QWidget(mainToolBar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  mainToolBar->addWidget(spacer);
  mainToolBar->addAction(playAction);
  mainToolBar->addAction(pauseAction);
  mainToolBar->addAction(rewindAction);

  // Disable toolbar context menu
  mainToolBar->setContextMenuPolicy(Qt::PreventContextMenu);

  // Toggle show/hide of toolbar
  showToolBarAction = mainToolBar->toggleViewAction();
  toolBarsMenu->addAction(showToolBarAction);

  // Add toolbars.
  addToolBar(Qt::TopToolBarArea, mainToolBar);

  // XXX: style hack
  mainToolBar->setStyleSheet("border-bottom: solid 5px #272a36;");
}

void MainWindow::createStatusBar()
{
  // Create canvases zoom level statut
  destinationZoomLabel = new QLabel(statusBar());
  destinationZoomLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  destinationZoomLabel->setContentsMargins(2, 0, 0, 0);
  sourceZoomLabel = new QLabel(statusBar());
  sourceZoomLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  sourceZoomLabel->setContentsMargins(2, 0, 0, 0);
  // Undoview statut
  undoLabel = new QLabel(statusBar());
  undoLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  undoLabel->setContentsMargins(2, 0, 0, 0);
  // Standard message
  currentMessageLabel = new QLabel(statusBar());
  currentMessageLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  currentMessageLabel->setContentsMargins(0, 0, 0, 0);
  // Current location of the mouse
  mousePosLabel = new QLabel(statusBar());
  mousePosLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  mousePosLabel->setContentsMargins(2, 0, 0, 0);

  // Add permanently into the statut bar
  statusBar()->addPermanentWidget(currentMessageLabel, 5);
  statusBar()->addPermanentWidget(undoLabel, 4);
  statusBar()->addPermanentWidget(mousePosLabel, 3);
  statusBar()->addPermanentWidget(sourceZoomLabel, 1);
  statusBar()->addPermanentWidget(destinationZoomLabel, 1);

  // Update the status bar
  updateStatusBar();
}

void MainWindow::readSettings()
{
  // FIXME: for each setting that is new since the first release in the major version number branch,
  // make sure it exists before reading its value.
  QSettings settings("MapMap", "MapMap");

  // settings present since 0.1.0:
  restoreGeometry(settings.value("geometry").toByteArray());
  restoreState(settings.value("windowState").toByteArray());
  
  mainSplitter->restoreState(settings.value("mainSplitter").toByteArray());
  paintSplitter->restoreState(settings.value("paintSplitter").toByteArray());
  mappingSplitter->restoreState(settings.value("mappingSplitter").toByteArray());
  canvasSplitter->restoreState(settings.value("canvasSplitter").toByteArray());
  outputWindow->restoreGeometry(settings.value("outputWindow").toByteArray());

  // new in 0.1.2:
  if (settings.contains("displayOutputWindow"))
  {
    outputFullScreenAction->setChecked(settings.value("displayOutputWindow").toBool());
    outputWindow->setFullScreen(outputFullScreenAction->isChecked());
  }
  if (settings.contains("displayTestSignal"))
  {
    displayTestSignalAction->setChecked(settings.value("displayTestSignal").toBool());
    enableTestSignal(displayTestSignalAction->isChecked());
  }
  if (settings.contains("displayControls"))
  {
    displayControlsAction->setChecked(settings.value("displayControls").toBool());
    outputWindow->setDisplayCrosshair(displayControlsAction->isChecked());
  }

  config_osc_receive_port = settings.value("osc_receive_port", 12345).toInt();

  // Update Recent files and video
  updateRecentFileActions();
  updateRecentVideoActions();

  // new in 0.3.2
  if (settings.contains("displayUndoStack"))
    displayUndoStackAction->setChecked(settings.value("displayUndoStack").toBool());
  if (settings.contains("zoomToolBar"))
    displayZoomToolAction->setChecked(settings.value("zoomToolBar").toBool());
  if (settings.contains("showMenuBar"))
    showMenuBarAction->setChecked(settings.value("showMenuBar").toBool());
}

void MainWindow::writeSettings()
{
  QSettings settings("MapMap", "MapMap");

  settings.setValue("geometry", saveGeometry());
  settings.setValue("windowState", saveState());
  
  settings.setValue("mainSplitter", mainSplitter->saveState());
  settings.setValue("paintSplitter", paintSplitter->saveState());
  settings.setValue("mappingSplitter", mappingSplitter->saveState());
  settings.setValue("canvasSplitter", canvasSplitter->saveState());
  settings.setValue("outputWindow", outputWindow->saveGeometry());
  settings.setValue("displayOutputWindow", outputFullScreenAction->isChecked());
  settings.setValue("displayTestSignal", displayTestSignalAction->isChecked());
  settings.setValue("displayControls", displayControlsAction->isChecked());
  settings.setValue("osc_receive_port", config_osc_receive_port);
  settings.setValue("displayUndoStack", displayUndoStackAction->isChecked());
  settings.setValue("zoomToolBar", displayZoomToolAction->isChecked());
  settings.setValue("showMenuBar", showMenuBarAction->isChecked());
}

bool MainWindow::okToContinue()
{
  if (isWindowModified())
  {
    int r = QMessageBox::warning(this, tr("MapMap"),
                                 tr("The document has been modified.\n"
                                    "Do you want to save your changes?"),
                                 QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (r == QMessageBox::Yes)
    {
      return save();
    }
    else if (r == QMessageBox::Cancel)
    {
      return false;
    }
  }
  return true;
}

bool MainWindow::loadFile(const QString &fileName)
{
  QFile file(fileName);
  QDir currentDir;

  if (! file.open(QFile::ReadOnly | QFile::Text))
  {
    QMessageBox::warning(this, tr("Error reading mapping project file"),
                         tr("Cannot read file %1:\n%2.")
                         .arg(fileName)
                         .arg(file.errorString()));
    return false;
  }

  // Clear current project.
  clearProject();

  // Read new project
  ProjectReader reader(this);
  if (! reader.readFile(&file))
  {
    QMessageBox::warning(this, tr("Error reading mapping project file"),
                         tr("Parse error in file %1:\n\n%2")
                         .arg(fileName)
                         .arg(reader.errorString()));
  }
  else
  {
    settings.setValue("defaultProjectDir", currentDir.absoluteFilePath(fileName));
    statusBar()->showMessage(tr("File loaded"), 2000);
    setCurrentFile(fileName);
  }

  return true;
}

bool MainWindow::saveFile(const QString &fileName)
{
  QFile file(fileName);
  if (! file.open(QFile::WriteOnly | QFile::Text))
  {
    QMessageBox::warning(this, tr("Error saving mapping project"),
                         tr("Cannot write file %1:\n%2.")
                         .arg(fileName)
                         .arg(file.errorString()));
    return false;
  }

  ProjectWriter writer(this);
  if (writer.writeFile(&file))
  {
    setCurrentFile(fileName);
    statusBar()->showMessage(tr("File saved"), 2000);
    return true;
  }
  else
    return false;
}

void MainWindow::setCurrentFile(const QString &fileName)
{
  curFile = fileName;
  setWindowModified(false);

  QString shownName = tr("Untitled");
  if (!curFile.isEmpty())
  {
    shownName = strippedName(curFile);
    recentFiles = settings.value("recentFiles").toStringList();
    recentFiles.removeAll(curFile);
    recentFiles.prepend(curFile);
    while (recentFiles.size() > MaxRecentFiles)
    {
      recentFiles.removeLast();
    }
    settings.setValue("recentFiles", recentFiles);
    updateRecentFileActions();
  }

  setWindowTitle(tr("%1[*] - %2").arg(shownName).arg(tr("MapMap Project")));
}

void MainWindow::setCurrentVideo(const QString &fileName)
{
  curVideo = fileName;

  recentVideos = settings.value("recentVideos").toStringList();
  recentVideos.removeAll(curVideo);
  recentVideos.prepend(curVideo);
  while (recentVideos.size() > MaxRecentVideo)
    recentVideos.removeLast();
  settings.setValue("recentVideos", recentVideos);
  updateRecentVideoActions();
}

void MainWindow::updateRecentFileActions()
{
  recentFiles = settings.value("recentFiles").toStringList();
  int numRecentFiles = qMin(recentFiles.size(), int(MaxRecentFiles));

  for (int j = 0; j < numRecentFiles; ++j)
  {
    QString text = tr("&%1 %2")
        .arg(j + 1)
        .arg(strippedName(recentFiles[j]));
    recentFileActions[j]->setText(text);
    recentFileActions[j]->setData(recentFiles[j]);
    recentFileActions[j]->setVisible(true);
    clearRecentFileActions->setVisible(true);
  }

  for (int i = numRecentFiles; i < MaxRecentFiles; ++i)
  {
    recentFileActions[i]->setVisible(false);
  }

  if (numRecentFiles > 0)
  {
    separatorAction->setVisible(true);
    clearRecentFileActions->setText(tr("Clear List"));
    clearRecentFileActions->setEnabled(true);
  } else {
    clearRecentFileActions->setText(tr("No Recents Projects"));
    clearRecentFileActions->setEnabled(false);
  }
}

void MainWindow::updateRecentVideoActions()
{
  recentVideos = settings.value("recentVideos").toStringList();
  int numRecentVideos = qMin(recentVideos.size(), int(MaxRecentVideo));

  for (int i = 0; i < numRecentVideos; ++i)
  {
    QString text = tr("&%1 %2")
        .arg(i + 1)
        .arg(strippedName(recentVideos[i]));
    recentVideoActions[i]->setText(text);
    recentVideoActions[i]->setData(recentVideos[i]);
    recentVideoActions[i]->setVisible(true);
  }

  for (int j = numRecentVideos; j < MaxRecentVideo; ++j)
    recentVideoActions[j]->setVisible(false);

  if (numRecentVideos >  0)
  {
    emptyRecentVideos->setVisible(false);
  }
}

void MainWindow::clearRecentFileList()
{
  recentFiles = settings.value("recentFiles").toStringList();

  while (recentFiles.size() > 0)
    recentFiles.clear();

  settings.setValue("recentFiles", recentFiles);
  updateRecentFileActions();
}

// TODO
// bool MainWindow::updateMediaFile(const QString &source_name, const QString &fileName)
// {
// }

bool MainWindow::importMediaFile(const QString &fileName, bool isImage)
{
  QFile file(fileName);
  QDir currentDir;

  if (!fileSupported(fileName, isImage))
    return false;

  bool live = false;
  if (!file.open(QIODevice::ReadOnly)) {
    if (file.isSequential())
      live = true;
    else {
      QMessageBox::warning(this, tr("MapMap Project"),
                           tr("Cannot read file %1:\n%2.")
                           .arg(file.fileName())
                           .arg(file.errorString()));
      return false;
    }
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);

  // Add media file to model.
  uint mediaId = createMediaPaint(NULL_UID, fileName, 0, 0, isImage, live);

  // Initialize position (center).
  QSharedPointer<Video> media = qSharedPointerCast<Video>(mappingManager->getPaintById(mediaId));
  Q_CHECK_PTR(media);

  if (_isPlaying)
    media->play();
  else
    media->pause();

  media->setPosition((sourceCanvas->width()  - media->getWidth() ) / 2.0f,
                     (sourceCanvas->height() - media->getHeight()) / 2.0f );

  QApplication::restoreOverrideCursor();

  if (!isImage)
  {
    settings.setValue("defaultVideoDir", currentDir.absoluteFilePath(fileName));
    setCurrentVideo(fileName);
  }
  else
  {
    settings.setValue("defaultImageDir", currentDir.absoluteFilePath(fileName));
  }

  statusBar()->showMessage(tr("File imported"), 2000);

  return true;
}

bool MainWindow::addColorPaint(const QColor& color)
{
  QApplication::setOverrideCursor(Qt::WaitCursor);

  // Add color to model.
  uint colorId = createColorPaint(NULL_UID, color);

  // Initialize position (center).
  QSharedPointer<Color> colorPaint = qSharedPointerCast<Color>(mappingManager->getPaintById(colorId));
  Q_CHECK_PTR(colorPaint);

  // Does not do anything...
  if (_isPlaying)
    colorPaint->play();
  else
    colorPaint->pause();

  QApplication::restoreOverrideCursor();

  statusBar()->showMessage(tr("Color paint added"), 2000);

  return true;
}

void MainWindow::addPaintItem(uid paintId, const QIcon& icon, const QString& name)
{
  Paint::ptr paint = mappingManager->getPaintById(paintId);
  Q_CHECK_PTR(paint);

  // Create paint gui.
  PaintGui::ptr paintGui;
  QString paintType = paint->getType();
  if (paintType == "media")
    paintGui = PaintGui::ptr(new VideoGui(paint));
  else if (paintType == "image")
    paintGui = PaintGui::ptr(new ImageGui(paint));
  else if (paintType == "color")
    paintGui = PaintGui::ptr(new ColorGui(paint));
  else
    paintGui = PaintGui::ptr(new PaintGui(paint));

  // Add to list of paint guis..
  paintGuis[paintId] = paintGui;
  QWidget* paintEditor = paintGui->getPropertiesEditor();
  paintPropertyPanel->addWidget(paintEditor);
  paintPropertyPanel->setCurrentWidget(paintEditor);
  paintPropertyPanel->setEnabled(true);

  // When paint value is changed, update canvases.
  //  connect(paintGui.get(), SIGNAL(valueChanged()),
  //          this,           SLOT(updateCanvases()));

  connect(paintGui.data(), SIGNAL(valueChanged(Paint::ptr)),
          this,            SLOT(handlePaintChanged(Paint::ptr)));

  connect(paint.data(), SIGNAL(propertyChanged(uid, QString, QVariant)),
          this,           SLOT(paintPropertyChanged(uid, QString, QVariant)));

  // TODO: attention: if mapping is invisible canvases will be updated for no reason
  connect(paint.data(), SIGNAL(propertyChanged(uid, QString, QVariant)),
          this,           SLOT(updateCanvases()));

  // Add paint item to paintList widget.
  QListWidgetItem* item = new QListWidgetItem(icon, name);
  setItemId(*item, paintId); // TODO: could possibly be replaced by a Paint pointer

  // Set size.
  item->setSizeHint(QSize(item->sizeHint().width(), MainWindow::PAINT_LIST_ITEM_HEIGHT));

  // Switch to paint tab.
  contentTab->setCurrentWidget(paintSplitter);

  // Add item to paint list.
  paintList->addItem(item);
  paintList->setCurrentItem(item);

  // Window was modified.
  windowModified();
}

void MainWindow::updatePaintItem(uid paintId, const QIcon& icon, const QString& name) {
  QListWidgetItem* item = getItemFromId(*paintList, paintId);
  Q_ASSERT(item);

  // Update item info.
  item->setIcon(icon);
  item->setText(name);

  // Window was modified.
  windowModified();
}

void MainWindow::addMappingItem(uid mappingId)
{
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);
  Q_CHECK_PTR(mapping);

  QString label;
  QIcon icon;

  QString shapeType = mapping->getShape()->getType();
  QString paintType = mapping->getPaint()->getType();

  // Add mapper.
  // XXX hardcoded for textures
  QSharedPointer<TextureMapping> textureMapping;
  if (paintType == "media" || paintType == "image")
  {
    textureMapping = qSharedPointerCast<TextureMapping>(mapping);
    Q_CHECK_PTR(textureMapping);
  }

  MappingGui::ptr mapper;

  // XXX Branching on nVertices() is crap

  // Triangle
  if (shapeType == "triangle")
  {
    label = QString("Triangle %1").arg(mappingId);
    icon = QIcon(":/shape-triangle");

    if (paintType == "color")
      mapper = MappingGui::ptr(new PolygonColorMappingGui(mapping));
    else
      mapper = MappingGui::ptr(new TriangleTextureMappingGui(textureMapping));
  }
  // Mesh
  else if (shapeType == "mesh" || shapeType == "quad")
  {
    label = QString(shapeType == "mesh" ? "Mesh %1" : "Quad %1").arg(mappingId);
    icon = QIcon(":/shape-mesh");
    if (paintType == "color")
      mapper = MappingGui::ptr(new PolygonColorMappingGui(mapping));
    else
      mapper = MappingGui::ptr(new MeshTextureMappingGui(textureMapping));
  }
  else if (shapeType == "ellipse")
  {
    label = QString("Ellipse %1").arg(mappingId);
    icon = QIcon(":/shape-ellipse");
    if (paintType == "color")
      mapper = MappingGui::ptr(new EllipseColorMappingGui(mapping));
    else
      mapper = MappingGui::ptr(new EllipseTextureMappingGui(textureMapping));
  }
  else
  {
    label = QString("Polygon %1").arg(mappingId);
    icon = QIcon(":/shape-polygon");
  }

  // Label is only going to be applied if no name is present.
  if (!mapping->getName().isEmpty())
    label = mapping->getName();

  // Add to list of mappers.
  mappers[mappingId] = mapper;
  QWidget* mapperEditor = mapper->getPropertiesEditor();
  mappingPropertyPanel->addWidget(mapperEditor);
  mappingPropertyPanel->setCurrentWidget(mapperEditor);
  mappingPropertyPanel->setEnabled(true);

  // When mapper value is changed, update canvases.
  connect(mapper.data(), SIGNAL(valueChanged()),
          this,          SLOT(updateCanvases()));

  connect(sourceCanvas,  SIGNAL(shapeChanged(MShape*)),
          mapper.data(), SLOT(updateShape(MShape*)));

  connect(destinationCanvas, SIGNAL(shapeChanged(MShape*)),
          mapper.data(),     SLOT(updateShape(MShape*)));

  connect(mapping.data(), SIGNAL(propertyChanged(uid, QString, QVariant)),
          this,           SLOT(mappingPropertyChanged(uid, QString, QVariant)));

  // TODO: attention: if mapping is invisible canvases will be updated for no reason
  connect(mapping.data(), SIGNAL(propertyChanged(uid, QString, QVariant)),
          this,           SLOT(updateCanvases()));

  // Switch to mapping tab.
  contentTab->setCurrentWidget(mappingSplitter);

  // Add item to layerList widget.
  mappingListModel->addItem(icon, label, mappingId);
  mappingListModel->updateModel();
  setCurrentMapping(mappingId);

  // Disable Test signal when add Shapes
  enableTestSignal(false);

  // Add items to scenes.
  if (mapper->getInputGraphicsItem())
    sourceCanvas->scene()->addItem(mapper->getInputGraphicsItem().data());
  if (mapper->getGraphicsItem())
    destinationCanvas->scene()->addItem(mapper->getGraphicsItem().data());

  // Window was modified.
  windowModified();
}

void MainWindow::removeMappingItem(uid mappingId)
{
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);
  Q_CHECK_PTR(mapping);

  // Remove mapping from model.
  mappingManager->removeMapping(mappingId);

  // Remove associated mapper.
  mappingPropertyPanel->removeWidget(mappers[mappingId]->getPropertiesEditor());
  mappers.remove(mappingId);

  // Remove widget from mappingList.
  int row = mappingListModel->getItemRowFromId(mappingId);
  Q_ASSERT( row >= 0 );
  mappingListModel->removeItem(row);

  // Update list.
  mappingListModel->updateModel();

  int nextSelectedRow = row == mappingListModel->rowCount() ? row - 1 : row;
  QModelIndex index = mappingListModel->getIndexFromRow(nextSelectedRow);
  mappingList->selectionModel()->select(index, QItemSelectionModel::Select);
  mappingList->setCurrentIndex(index);

  // Update everything.
  updateCanvases();

  // Window was modified.
  windowModified();
}

void MainWindow::removePaintItem(uid paintId)
{
  Paint::ptr paint = mappingManager->getPaintById(paintId);
  Q_CHECK_PTR(paint);

  // Remove all mappings associated with paint.
  QMap<uid, Mapping::ptr> paintMappings = mappingManager->getPaintMappings(paint);
  for (QMap<uid, Mapping::ptr>::const_iterator it = paintMappings.constBegin();
       it != paintMappings.constEnd(); ++it) {
    removeMappingItem(it.key());
  }
  // Remove paint from model.
  Q_ASSERT( mappingManager->removePaint(paintId) );

  // Remove associated mapper.
  paintPropertyPanel->removeWidget(paintGuis[paintId]->getPropertiesEditor());
  paintGuis.remove(paintId);

  // Remove widget from paintList.
  int row = getItemRowFromId(*paintList, paintId);
  Q_ASSERT( row >= 0 );
  QListWidgetItem* item = paintList->takeItem(row);
  if (item == currentSelectedItem)
    currentSelectedItem = NULL;
  delete item;

  // Update list.
  paintList->update();

  // Reset current paint.
  removeCurrentPaint();

  // Update everything.
  updateCanvases();

  // Window was modified.
  windowModified();
  // Build mapping!
  // FIXME: mapping->build(); // I removed this 2014-04-25
}

void MainWindow::clearWindow()
{
  clearProject();
}

bool MainWindow::fileExists(const QString &file)
{
  QFileInfo checkFile(file);

  if (checkFile.exists() && checkFile.isFile())
    return true;

  return false;
}

bool MainWindow::fileSupported(const QString &file, bool isImage)
{
  QFileInfo fileInfo(file);
  QString fileExtension = fileInfo.suffix();

  if (isImage) {
    if (MM::IMAGE_FILES_FILTER.contains(fileExtension, Qt::CaseInsensitive))
      return true;
  } else {
    if (MM::VIDEO_FILES_FILTER.contains(fileExtension, Qt::CaseInsensitive))
      return true;
  }

  QMessageBox::warning(this, tr("Warning"),
                       tr("The following file is not supported: %1")
                       .arg(fileInfo.fileName()));
  return false;
}

QString MainWindow::locateMediaFile(const QString &uri, bool isImage)
{
  // Get more info about url
  QFileInfo file(uri);
  // The name of the file
  QString filename = file.fileName();
  // The directory name
  QString directory = file.absolutePath();
  // Handle the case where it is video or image
  QString mediaFilter = isImage ? MM::IMAGE_FILES_FILTER : MM::VIDEO_FILES_FILTER;
  QString mediaType = isImage ? "Images" : "Videos";
  // New linked uri
  QString url;

  // Show a warning and offer to locate the file
  QMessageBox::warning(this,
                       tr("Cannot load movie"),
                       tr("Unable to use the file « %1 » \n"
                          "The original file is not found. Will you locate?")
                       .arg(filename));

  // Set the new uri
  url = QFileDialog::getOpenFileName(this,
                                     tr("Locate file « %1 »").arg(filename),
                                     directory,
                                     tr("%1 files (%2)")
                                     .arg(mediaType)
                                     .arg(mediaFilter));

  return url;
}

MainWindow* MainWindow::instance() {
  static MainWindow* inst = 0;
  if (!inst) {
    inst = new MainWindow;
  }
  return inst;
}

void MainWindow::updateCanvases()
{
  // Update scenes.
  sourceCanvas->scene()->update();
  destinationCanvas->scene()->update();

  // Update canvases.
  sourceCanvas->update();
  destinationCanvas->update();
  outputWindow->getCanvas()->update();

  // Update statut bar
  updateStatusBar();
}

void MainWindow::enableDisplayControls(bool display)
{
  _displayControls = display;
  updateCanvases();
}

void MainWindow::enableTestSignal(bool enable)
{
  updateCanvases();
}

void MainWindow::displayUndoStack(bool display)
{
  _displayUndoStack = display;

  // Create undo view.
  undoView = new QUndoView(getUndoStack(), this);

  if (display) {
    contentTab->addTab(undoView, tr("Undo stack"));
  } else {
    contentTab->removeTab(2);
  }
}

void MainWindow::enableStickyVertices(bool value)
{
  _stickyVertices = value;
}

void MainWindow::showMappingContextMenu(const QPoint &point)
{
  QWidget *objectSender = static_cast<QWidget*>(sender());
  uid mappingId = currentMappingItemId();
  Mapping::ptr mapping = mappingManager->getMappingById(mappingId);

  // Switch to right action check state
  mappingLockedAction->setChecked(mapping->isLocked());
  mappingHideAction->setChecked(!mapping->isVisible());
  mappingSoloAction->setChecked(mapping->isSolo());

  if (objectSender != NULL) {
    if (sender() == mappingItemDelegate) // XXX: The item delegate is not a widget
      mappingContextMenu->exec(mappingList->mapToGlobal(point));
    else
      mappingContextMenu->exec(objectSender->mapToGlobal(point));
  }
}

void MainWindow::showPaintContextMenu(const QPoint &point)
{
  QWidget *objectSender = dynamic_cast<QWidget*>(sender());

  if (objectSender != NULL && paintList->count() > 0)
    paintContextMenu->exec(objectSender->mapToGlobal(point));
}

void MainWindow::play()
{
  // Update buttons.
  playAction->setVisible(false);
  pauseAction->setVisible(true);
  _isPlaying = true;

  // Start all paints.
  for (int i=0; i<mappingManager->nPaints(); i++)
    mappingManager->getPaint(i)->play();
}

void MainWindow::pause()
{
  // Update buttons.
  playAction->setVisible(true);
  pauseAction->setVisible(false);
  _isPlaying = false;

  // Pause all paints.
  for (int i=0; i<mappingManager->nPaints(); i++)
    mappingManager->getPaint(i)->pause();
}

void MainWindow::rewind()
{
  // Rewind all paints.
  for (int i=0; i<mappingManager->nPaints(); i++)
    mappingManager->getPaint(i)->rewind();
}

QString MainWindow::strippedName(const QString &fullFileName)
{
  return QFileInfo(fullFileName).fileName();
}

void MainWindow::connectProjectWidgets()
{
  connect(paintList, SIGNAL(itemSelectionChanged()),
          this,      SLOT(handlePaintItemSelectionChanged()));

  connect(paintList, SIGNAL(itemPressed(QListWidgetItem*)),
          this,      SLOT(handlePaintItemSelected(QListWidgetItem*)));

  connect(paintList, SIGNAL(itemActivated(QListWidgetItem*)),
          this,      SLOT(handlePaintItemSelected(QListWidgetItem*)));
  // Rename Paint with double click
  connect(paintList, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
          this,      SLOT(renamePaintItem()));
  // When finish to edit mapping item
  connect(paintList->itemDelegate(), SIGNAL(commitData(QWidget*)),
          this, SLOT(paintListEditEnd(QWidget*)));

  connect(mappingList->selectionModel(), SIGNAL(currentRowChanged(QModelIndex,QModelIndex)),
          this,        SLOT(handleMappingItemSelectionChanged(QModelIndex)));

  connect(mappingListModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)),
          this,        SLOT(handleMappingItemChanged(QModelIndex)));
          
  connect(mappingListModel, SIGNAL(rowsMoved(QModelIndex,int,int,QModelIndex,int)),
          this,                 SLOT(handleMappingIndexesMoved()));
          
  connect(mappingItemDelegate, SIGNAL(itemDuplicated(uid)),
          this, SLOT(duplicateMapping(uid)));
          
  connect(mappingItemDelegate, SIGNAL(itemRemoved(uid)),
          this, SLOT(deleteMapping(uid)));
}

void MainWindow::disconnectProjectWidgets()
{
  disconnect(paintList, SIGNAL(itemSelectionChanged()),
             this,      SLOT(handlePaintItemSelectionChanged()));

  disconnect(paintList, SIGNAL(itemPressed(QListWidgetItem*)),
             this,      SLOT(handlePaintItemSelected(QListWidgetItem*)));

  disconnect(paintList, SIGNAL(itemActivated(QListWidgetItem*)),
             this,      SLOT(handlePaintItemSelected(QListWidgetItem*)));

  disconnect(mappingList->selectionModel(), SIGNAL(currentRowChanged(QModelIndex,QModelIndex)),
          this,        SLOT(handleMappingItemSelectionChanged(QModelIndex)));

  disconnect(mappingListModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)),
          this,        SLOT(handleMappingItemChanged(QModelIndex)));
          
  disconnect(mappingListModel, SIGNAL(rowsMoved(QModelIndex,int,int,QModelIndex,int)),
          this,                 SLOT(handleMappingIndexesMoved()));
          
  disconnect(mappingItemDelegate, SIGNAL(itemDuplicated(uid)),
          this, SLOT(duplicateMapping(uid)));
          
  connect(mappingItemDelegate, SIGNAL(itemRemoved(uid)),
          this, SLOT(deleteMapping(uid)));
}

uid MainWindow::getItemId(const QListWidgetItem& item)
{
  return item.data(Qt::UserRole).toInt();
}

void MainWindow::setItemId(QListWidgetItem& item, uid id)
{
  item.setData(Qt::UserRole, id);
}

QListWidgetItem* MainWindow::getItemFromId(const QListWidget& list, uid id) {
  int row = getItemRowFromId(list, id);
  if (row >= 0)
    return list.item( row );
  else
    return NULL;
}

int MainWindow::getItemRowFromId(const QListWidget& list, uid id)
{
  for (int row=0; row<list.count(); row++)
  {
    QListWidgetItem* item = list.item(row);
    if (getItemId(*item) == id)
      return row;
  }

  return (-1);
}

uid MainWindow::currentMappingItemId() const
{
  return mappingListModel->getItemId(currentSelectedIndex);
}

QIcon MainWindow::createColorIcon(const QColor &color) {
  QPixmap pixmap(100,100);
  pixmap.fill(color);
  return QIcon(pixmap);
}

QIcon MainWindow::createFileIcon(const QString& filename) {
  static QFileIconProvider provider;
  return provider.icon(QFileInfo(filename));
}

QIcon MainWindow::createImageIcon(const QString& filename) {
  return QIcon(filename);
}


void MainWindow::setCurrentPaint(int uid)
{
  if (uid == NULL_UID)
    removeCurrentPaint();
  else {
    if (currentPaintId != uid) {
      currentPaintId = uid;
      paintList->setCurrentRow( getItemRowFromId(*paintList, uid) );
      paintPropertyPanel->setCurrentWidget(paintGuis[uid]->getPropertiesEditor());
    }
    _hasCurrentPaint = true;
  }
}

void MainWindow::setCurrentMapping(int uid)
{
  if (uid == NULL_UID)
    removeCurrentMapping();
  else {
    if (currentMappingId != uid) {
      currentMappingId = uid;
      currentSelectedIndex = mappingListModel->getIndexFromRow(mappingListModel->getItemRowFromId(uid));
      mappingList->setCurrentIndex(currentSelectedIndex);
      mappingPropertyPanel->setCurrentWidget(mappers[uid]->getPropertiesEditor());
    }
    _hasCurrentMapping = true;
  }
}

void MainWindow::removeCurrentPaint() {
  _hasCurrentPaint = false;
  currentPaintId = NULL_UID;
  paintList->clearSelection();
}

void MainWindow::removeCurrentMapping() {
  _hasCurrentMapping = false;
  currentMappingId = NULL_UID;
  mappingList->clearSelection();
}

void MainWindow::startOscReceiver()
{
#ifdef HAVE_OSC
  int port = config_osc_receive_port;
  std::ostringstream os;
  os << port;
#if QT_VERSION >= 0x050500
  QMessageLogger(__FILE__, __LINE__, 0).info() << "OSC port: " << port ;
#else
  QMessageLogger(__FILE__, __LINE__, 0).debug() << "OSC port: " << port ;
#endif
  osc_interface.reset(new OscInterface(os.str()));
  if (port != 0)
  {
    osc_interface->start();
  }
  osc_timer = new QTimer(this); // FIXME: memleak?
  connect(osc_timer, SIGNAL(timeout()), this, SLOT(pollOscInterface()));
  osc_timer->start();
#endif
}

bool MainWindow::setOscPort(int portNumber)
{
  return this->setOscPort(QString::number(portNumber));
}

int MainWindow::getOscPort() const
{
  return config_osc_receive_port;
}

bool MainWindow::setOscPort(QString portNumber)
{
  if (Util::isNumeric(portNumber))
  {
    int port = portNumber.toInt();
    if (port <= 1023 || port > 65535)
    {
      std::cout << "OSC port is out of range: " << portNumber.toInt() << std::endl;
      return false;
    }
    config_osc_receive_port = port;
    startOscReceiver();
  }
  else
  {
    std::cout << "OSC port is not a number: " << portNumber.toInt() << std::endl;
    return false;
  }
  return true;
}

void MainWindow::pollOscInterface()
{
#ifdef HAVE_OSC
  osc_interface->consume_commands(*this);
#endif
}

void MainWindow::exitFullScreen()
{
  outputFullScreenAction->setChecked(false);
}

// void MainWindow::applyOscCommand(const QVariantList& command)
// {
//   bool VERBOSE = true;
//   if (VERBOSE)
//   {
//     std::cout << "Receive OSC: ";
//     for (int i = 0; i < command.size(); ++i)
//     {
//       if (command.at(i).type()  == QVariant::Int)
//       {
//         std::cout << command.at(i).toInt() << " ";
//       }
//       else if (command.at(i).type()  == QVariant::Double)
//       {
//         std::cout << command.at(i).toDouble() << " ";
//       }
//       else if (command.at(i).type()  == QVariant::String)
//       {
//         std::cout << command.at(i).toString().toStdString() << " ";
//       }
//       else
//       {
//         std::cout << "??? ";
//       }
//     }
//     std::cout << std::endl;
//     std::cout.flush();
//   }
//
//   if (command.size() < 2)
//       return;
//   if (command.at(0).type() != QVariant::String)
//       return;
//   if (command.at(1).type() != QVariant::String)
//       return;
//   std::string path = command.at(0).toString().toStdString();
//   std::string typetags = command.at(1).toString().toStdString();
//
//   // Handle all OSC messages here
//   if (path == "/image/uri" && typetags == "s")
//   {
//       std::string image_uri = command.at(2).toString().toStdString();
//       std::cout << "TODO load /image/uri " << image_uri << std::endl;
//   }
//   else if (path == "/add/quad")
//       addMesh();
//   else if (path == "/add/triangle")
//       addTriangle();
//   else if (path == "/add/ellipse")
//       addEllipse();
//   else if (path == "/project/save")
//       save();
//   else if (path == "/project/open")
//       open();
// }

bool MainWindow::setTextureUri(int texture_id, const std::string &uri)
{
  // TODO: const QString &

  bool success = false;
  Paint::ptr paint = this->mappingManager->getPaintById(texture_id);
  if (paint.isNull())
  {
    std::cout << "No such texture paint id " << texture_id << std::endl;
    success = false;
  }
  else
  {
    if (paint->getType() == "media")
    {
      Video *media = static_cast<Video*>(paint.data()); // FIXME: use sharedptr cast
      videoTimer->stop();
      success = media->setUri(QString(uri.c_str()));
      videoTimer->start();
    }
    else if (paint->getType() == "image")
    {
      Image *media = (Image*) paint.data(); // FIXME: use sharedptr cast
      videoTimer->stop();
      success = media->setUri(QString(uri.c_str()));
      videoTimer->start();
    }
    else
    {
      std::cout << "Paint id " << texture_id << " is not a media texture." << std::endl;
      return false;
    }
  }
  return success;
}

bool MainWindow::setTextureRate(int texture_id, double rate)
{
  Paint::ptr paint = this->mappingManager->getPaintById(texture_id);
  if (paint.isNull())
  {
    std::cout << "No such texture paint id " << texture_id << std::endl;
    return false;
  }
  else
  {
    if (paint->getType() == "media")
    {
      Video *media = static_cast<Video*>(paint.data()); // FIXME: use sharedptr cast
      videoTimer->stop();
      media->setRate(rate);
      videoTimer->start();
    }
    else
    {
      std::cout << "Paint id " << texture_id << " is not a media texture." << std::endl;
      return false;
    }
  }
  return true;
}

bool MainWindow::setTextureVolume(int texture_id, double volume)
{
  Paint::ptr paint = this->mappingManager->getPaintById(texture_id);
  if (paint.isNull())
  {
    std::cout << "No such texture paint id " << texture_id << std::endl;
    return false;
  }
  else
  {
    if (paint->getType() == "media")
    {
      Video *media = static_cast<Video*>(paint.data()); // FIXME: use sharedptr cast
      videoTimer->stop();
      media->setVolume(volume);
      videoTimer->start();
    }
    else
    {
      std::cout << "Paint id " << texture_id << " is not a media texture." << std::endl;
      return false;
    }
  }
  return true;
}

void MainWindow::setTexturePlayState(int texture_id, bool played)
{
  Paint::ptr paint = this->mappingManager->getPaintById(texture_id);
  if (paint.isNull())
  {
    std::cout << "No such texture paint id " << texture_id << std::endl;
  }
  else
  {
    if (paint->getType() == "media")
    {
      if (played)
      {
        videoTimer->stop();
        paint->play();
        videoTimer->start();
      }
      else
      {
        videoTimer->stop();
        paint->pause();
        videoTimer->start();
      }

    }
    else
    {
      std::cout << "Paint id " << texture_id << " is not a media texture." << std::endl;
    }
  }
}

void MainWindow::quitMapMap()
{
  close();
}

MM_END_NAMESPACE
