/*
    This file is part of QJsonTreeWidget.

    Copyright (C) 2012 valerino <valerio.lupi@te4i.com>

    QJsonTreeWidget is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    QJsonTreeWidget is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with QJsonTreeWidget.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qjsontreewidget.h"

QJsonTreeWidget::QJsonTreeWidget(QWidget *parent, Qt::WindowFlags f) :
  QWidget(parent,f)
{
  m_maxVersion = JSON_TREE_MAX_VERSION;
  m_currentSelection = QModelIndex();
  m_editing = true;
  m_purgeDescriptiveTags = false;
  m_root = 0;

  // create qjson objects
  m_parser = new QJson::Parser();
  m_serializer = new QJson::Serializer();
  m_serializer->setIndentMode(QJson::IndentNone);

  // create the view and organize this widget in a vertical layout
  QBoxLayout* l= new QVBoxLayout(this);
  m_view = new QTreeView(this);
  m_view->setHeaderHidden(false);
  m_view->setSortingEnabled(false);
  l->addWidget(m_view);

  // add an optional layout for the user to put its controls in
  m_optLayout = new QGridLayout(this);
  l->addLayout(m_optLayout);

  // set the delegate on the view
  m_delegate = new QJsonTreeItemDelegate(this);
  m_view->setItemDelegate(m_delegate);

  // add actions reachable by rightclicking on header
  m_view->setSelectionBehavior(QAbstractItemView::SelectItems);
  m_actionEnableSort = new QAction(tr("Enable sorting"),m_view);
  m_actionDisableSort = new QAction(tr("Disable sorting"),m_view);
  m_actionLoad = new QAction(tr("Load from JSON file"),m_view);
  m_actionSave = new QAction(tr("Save to JSON file"),m_view);
  m_actionSaveHtml = new QAction(tr("Save to HTML file"),m_view);
  m_actionEnableSort->setData("sortenable");
  m_actionDisableSort->setData("sortdisable");
  m_actionLoad->setData("load");
  m_actionSave->setData("save");
  m_actionSaveHtml->setData("savehtml");
  connect (m_actionLoad,SIGNAL(triggered()),this,SLOT(onActionLoad()));
  connect (m_actionSave,SIGNAL(triggered()),this,SLOT(onActionSave()));
  connect (m_actionSaveHtml,SIGNAL(triggered()),this,SLOT(onActionSaveHtml()));
  connect (m_actionEnableSort,SIGNAL(triggered()),this,SLOT(onActionEnableSort()));
  connect (m_actionDisableSort,SIGNAL(triggered()),this,SLOT(onActionDisableSort()));
  m_actionLoad->setVisible(false);
  m_actionDisableSort->setVisible(false);
  m_view->header()->addActions(QList<QAction*>() << m_actionEnableSort << m_actionDisableSort << m_actionLoad << m_actionSave << m_actionSaveHtml);
  m_view->header()->setContextMenuPolicy(Qt::ActionsContextMenu);

  // for mouseclicks
  connect (m_delegate,SIGNAL(clicked(const QJsonTreeItem*,QString)),this,SIGNAL(clicked(const QJsonTreeItem*,QString)));
  connect (m_delegate,SIGNAL(clicked(int,int,QString,QVariant,const QJsonTreeItem*)),this,SIGNAL(clicked(int,int,QString,QVariant,const QJsonTreeItem*)));

  // various options
  this->setContextMenuPolicy(Qt::CustomContextMenu);

  // create the model and proxy
  m_model = new QJsonTreeModel(this);
  connect (m_model,SIGNAL(dataChanged(QModelIndex,QModelIndex)),this,SLOT(onDataChanged(QModelIndex,QModelIndex)));
  m_proxyModel = new QJsonSortFilterProxyModel(this);
  m_proxyModel->setDynamicSortFilter(true);
  m_view->setModel(m_proxyModel);
}

QJsonTreeWidget::~QJsonTreeWidget()
{
  this->clear();
}

bool QJsonTreeWidget::loadJson(const QString &path)
{
  QFile file(path);
  file.open(QIODevice::ReadOnly);
  bool b = loadJson(file);
  file.close();
  return b;
}

bool QJsonTreeWidget::loadJson(QIODevice &dev)
{
  return loadJson(dev.readAll());
}

bool QJsonTreeWidget::loadJson(const QByteArray &buf)
{
  if (buf.isEmpty())
  {
    setNotFoundInvalidOrEmptyError("loadJson","buf");
    return false;
  }

  // parse
  bool ok;
  QVariantMap map = m_parser->parse(buf,&ok).toMap();
  if (!ok)
  {
      m_error = tr("loadJson: JSON parser error: line %1, %2").arg(QVariant(m_parser->errorLine()).toString()).arg(m_parser->errorString());
      return false;
  }

  return loadJson(map);
}

bool QJsonTreeWidget::loadJson(const QVariantMap &map)
{
    return loadJsonInternal(map);
}

bool QJsonTreeWidget::saveJson(const QString &path, QJson::IndentMode indentmode, const QVariantMap& additional)
{
  QFile file(path);
  bool b = file.open(QIODevice::WriteOnly);
  b = saveJson(file,indentmode,additional);
  file.close();
  return b;
}

bool QJsonTreeWidget::saveJson(QIODevice &dev, QJson::IndentMode indentmode, const QVariantMap& additional)
{
  QByteArray buf = saveJson(indentmode,additional);
  if (buf.isEmpty())
  {
    setNotFoundInvalidOrEmptyError("saveJson","buf");
    return false;
  }
  qint64 sz = dev.write(buf);
  if (sz != buf.size())
  {
    m_error = tr("saveJson: error writing, requested %1, written %2, QIODevice error: %3").arg(QVariant(buf.size()).toString()).arg(QVariant(sz).toString()).arg(dev.errorString());
    return false;
  }
  return true;
}

QByteArray QJsonTreeWidget::saveJson(QJson::IndentMode indentmode, const QVariantMap& additional)
{
  m_serializer->setIndentMode(indentmode);
  QVariantMap m = m_root->child(0)->toMap();
  foreach (QString key, additional.keys())
  {
      m[key]=additional[key];
  }

  return m_serializer->serialize(m);
}

int QJsonTreeWidget::jsonVersion(const QVariantMap map) const
{
    int v = map.value("version",-1).toInt();
    if (v == -1)
        return -1;

    return v;
}

void QJsonTreeWidget::resizeColumnsToContents()
{
  for (int i=0; i < this->invisibleRootItem()->columnCount(); i++)
  {
    resizeColumnToContent(i);
  }
}

void QJsonTreeWidget::clear()
{
  m_purgeList.clear();
  m_purgeDescriptiveTags = false;
  m_model->clear();
}

void QJsonTreeWidget::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
  Q_UNUSED(bottomRight);
  // since we emit onDataChanged with (index,index), topLeft is enough
  m_view->update(topLeft);
}

void QJsonTreeWidget::nextSelection()
{
  QModelIndexList l = m_view->selectionModel()->selectedIndexes();
  if (m_currentSelection.isValid())
  {
    // get the next
    int idx = l.lastIndexOf(m_currentSelection);
    idx++;
    if (idx > (l.count() - 1))
    {
      m_currentSelection = l.first();
    }
    else
    {
      m_currentSelection = l.at(idx);
    }

    // scroll to the current selection
    m_view->scrollTo(m_currentSelection);
  }
  else
  {
    m_currentSelection = l.first();

    // scroll to the current selection
    m_view->scrollTo(m_currentSelection);
  }
}

void QJsonTreeWidget::setNotFoundInvalidOrEmptyError(const QString &function, const QString &val)
{
  m_error=tr("%1: ERROR not found, empty or invalid:\n%2").arg(function).arg(val);
}

void QJsonTreeWidget::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_F3)
    nextSelection();
  else if (event->key() == Qt::Key_C && event->modifiers() & Qt::ControlModifier)
  {
    // copy to clipboard requested
    QModelIndex idx = m_proxyModel->mapToSource(m_view->selectionModel()->selectedIndexes().at(0));
    if (idx.isValid())
    {
      QJsonTreeItem* it = m_model->itemByModelIndex(idx);
      if (it)
        QApplication::clipboard()->setText(it->text(idx.column()));
    }
  }
}

bool QJsonTreeWidget::loadJsonInternal(const QVariantMap& map)
{
  this->clear();

  if (map.isEmpty())
  {
      setNotFoundInvalidOrEmptyError("loadJsonInternal","map");
      return false;
  }

  int v = jsonVersion(map);
  if (v == -1)
  {
    setNotFoundInvalidOrEmptyError("loadJsonInternal","_version_");
    return false;
  }
  if (v > m_maxVersion)
  {
    m_error = tr("loadJsonInternal: Unsupported JSON version: %1, maxversion: %2").arg(v).arg(m_maxVersion);
    return false;
  }

  QVariantMap maptouse = map;
  QVariantMap blob = map.value("_blob_",QVariantMap()).toMap();
  if (!blob.isEmpty())
  {
      // we use blob instead (for embedded trees into another map)
      maptouse = blob;
  }

  // create the root item. the root item is invisible, we use it only to store the headers hash.
  // so we need only _headers_ in it
  QString hdrstring = maptouse.value("_headers_",QString()).toString();
  if (hdrstring.isEmpty())
  {
    setNotFoundInvalidOrEmptyError("loadJsonInternal","_headers_");
    return false;
  }

  QVariantMap m;
  m["_headers_"]=hdrstring;
  m_root = new QJsonTreeItem(this,0,m);
  if (!m_root->isValid())
  {
    // something wrong with the invisible root item (probably header)
    QByteArray invalid = m_serializer->serialize(m_root->invalidMap());
    setNotFoundInvalidOrEmptyError("buildModel",invalid);
    this->clear();
    return false;
  }

  QJsonTreeItem* r = new QJsonTreeItem(this,m_root,maptouse); // this is the real root, 1st child of invisibleroot
  if (!m_root->isValid())
  {
    // something wrong with the real root item (probably header)
    QByteArray invalid = m_serializer->serialize(m_root->invalidMap());
    setNotFoundInvalidOrEmptyError("loadJsonInternal",invalid);
    this->clear();
    return false;
  }
  m_root->appendChild(r);

  m_model->setRoot(m_root);
  m_proxyModel->setSourceModel(m_model);
  return true;

}

void QJsonTreeWidget::setSortingEnabled(bool enable)
{
  m_view->setSortingEnabled(enable);
  if (!enable)
  {
    m_actionEnableSort->setVisible(true);
    m_actionDisableSort->setVisible(false);
    m_proxyModel->sort(-1);
  }
  else
  {
    // sorting enabled
    m_actionEnableSort->setVisible(false);
    m_actionDisableSort->setVisible(true);
  }
}

void QJsonTreeWidget::search(const QString& text)
{
  this->expandAll();
  m_proxyModel->setFilterRegExp(QRegExp(text,Qt::CaseInsensitive,QRegExp::FixedString));
  searchInternal();
}

void QJsonTreeWidget::search(const QRegExp& regex)
{
  m_proxyModel->setFilterRegExp(regex);
  searchInternal();
}

void QJsonTreeWidget::searchInternal()
{
  m_view->selectionModel()->select(QModelIndex(),QItemSelectionModel::Clear);
  m_proxyModel->setFilterKeyColumn(-1);
  this->setFocus();
  this->nextSelection();
}

void QJsonTreeWidget::onActionLoad()
{
  QString fname = QFileDialog::getOpenFileName(this, tr("Load JSON"),QString(),tr("JSON Files (*.json)"));
  if (!fname.isEmpty())
  {
    loadJson(fname);
    this->expandAll();
    this->resizeColumnToContent(0);
  }
}

void QJsonTreeWidget::onActionSave()
{
  QString fname = QFileDialog::getSaveFileName(this, tr("Save JSON"),QString(),tr("JSON Files (*.json)"));
  if (!fname.isEmpty())
  {
    saveJson(fname,QJson::IndentMinimum);
  }
}

void QJsonTreeWidget::onActionSaveHtml()
{
  QString fname = QFileDialog::getSaveFileName(this, tr("Save HTML"),QString(),tr("HTML Files (*.html)"));
  if (!fname.isEmpty())
  {
    toHtmlFile(fname);
  }
}

void QJsonTreeWidget::onActionEnableSort()
{
  setSortingEnabled(true);
}

void QJsonTreeWidget::onActionDisableSort()
{
  setSortingEnabled(false);
}

bool QJsonTreeWidget::validateItems(const QJsonTreeItem* item) const
{
  // whole tree ?
  if (item == 0)
  {
    item = m_root;
  }

  QString failname;
  QString failcol;
  QString failval;
  if (!item->validateRegexp(&failcol,&failname,&failval))
  {
    QMessageBox::warning(0,tr("Invalid input"), tr("Name: ") % failname % "\n" % tr("Column: ") % failcol % "\n" % tr("Text: ") % failval);
    return false;
  }
  if (item->childCount() > 0)
  {
    foreach (QJsonTreeItem* it, item->children())
    {
      // recurse
      if (!validateItems(it))
        return false;
    }
  }
  return true;
}

bool QJsonTreeWidget::findTag(const QString& tag, const QJsonTreeItem* item, QJsonTreeItem** found) const
{
  if (item == 0)
  {
    item = m_root;
  }

  if (item->map().contains(tag))
  {
    if (found)
    {
      *found = const_cast<QJsonTreeItem*>(item);
    }
    return true;
  }

  if (item->childCount() > 0)
  {
    foreach (QJsonTreeItem* it, item->children())
    {
      // recurse
      if (findTag(tag,it,found))
        return true;
    }
  }
  return false;
}

void QJsonTreeWidget::toHtmlEnd(QXmlStreamWriter* str, const QHash<QString, QString> div) const
{
  // close html
  str->writeEndElement(); // table
  if (!div.isEmpty())
  {
    str->writeEndElement(); // div
  }
  str->writeEndElement(); // body
  str->writeEndElement(); // html
  delete str;
}

QXmlStreamWriter* QJsonTreeWidget::toHtmlStart(QString *dest, const QString &title, const QHash<QString, QString> div, const QModelIndex& index) const
{
  QXmlStreamWriter* str;
  str = new QXmlStreamWriter(dest);
  str->setAutoFormatting(true);

  // html header
  str->writeStartElement("html");
  str->writeStartElement("head");
  if (!title.isEmpty())
  {
    str->writeTextElement("title",title);
  }
  str->writeEndElement(); // head

  // body
  str->writeStartElement("body");
  if (!div.isEmpty())
  {
    // div
    str->writeStartElement("div");
    foreach (QString k, div.keys())
    {
      str->writeAttribute(k,div[k]);
    }
  }

  // table
  str->writeStartElement("table");
  str->writeAttribute("border","1");
  str->writeStartElement("tr");
  QJsonTreeItem* item = m_model->itemByModelIndex(QJsonSortFilterProxyModel::indexToSourceIndex(index));
  for (int i=0; i < item->columnCount(); i++)
  {
    // headers
    str->writeTextElement("th",item->headerNameByIdx(i));
  }
  str->writeEndElement(); // tr

  return str;
}

void QJsonTreeWidget::toHtmlInternal(QXmlStreamWriter *str, const QModelIndex &index) const
{
  QJsonTreeItem* item = m_model->itemByModelIndex(QJsonSortFilterProxyModel::indexToSourceIndex(index));

  // depth spaces
  int depth = item->depth();
  QString spaces;
  for (int i=1; i < depth; i++)
  {
    spaces.append(" ");
  }

  str->writeStartElement("tr");
  for (int i=0; i < item->columnCount(); i++)
  {
    QString s = item->map().value(item->headerTagByIdx(i),QString()).toString();

    str->writeStartElement("td");
    if (item->hasChildren())
    {
      str->writeStartElement("b"); // parent bold
    }

    if (i == 0)
    {
      // column 0 has spacing
      if (!spaces.isEmpty())
      {
        str->writeStartElement("pre");
        str->writeCharacters(spaces);
      }
    }
    str->writeCharacters(s);
    if (!spaces.isEmpty() && i == 0)
    {
      str->writeEndElement(); // pre
    }
    if (item->hasChildren())
    {
      str->writeEndElement(); // b
    }
    str->writeEndElement(); // td

  }
  str->writeEndElement(); // tr

  // recurse
  int i = 0;
  while (true)
  {
    QModelIndex idx = index.child(i,0);
    if (!idx.isValid())
      break;
    toHtmlInternal(str,idx);
    i++;
  }
}

bool QJsonTreeWidget::toHtmlFile(const QString &path, const QString &title, const QHash<QString,QString> div, QJsonTreeItem *item) const
{
  QFile f (path);
  if (!f.open(QIODevice::WriteOnly))
    return false;

  QString s = toHtml(title,div,item);
  if (s.isEmpty())
  {
    f.close();
    return false;
  }
  f.write(s.toLatin1());
  f.close();
  return true;
}

void QJsonTreeWidget::setHeaderMenuEnabled(bool enable)
{
  if (enable)
  {
    m_view->header()->setContextMenuPolicy(Qt::ActionsContextMenu);
  }
  else
  {
    m_view->header()->setContextMenuPolicy(Qt::NoContextMenu);
  }
}

QString QJsonTreeWidget::toHtml(const QString &title, const QHash<QString,QString> div, QJsonTreeItem *item) const
{
  QModelIndex idx;

  if (!item)
  {
    // get the view's root
    idx = m_proxyModel->index(0,0,QModelIndex());
  }
  else
  {
    // specific item
    idx = QJsonSortFilterProxyModel::indexToProxyIndex(m_model->indexByItem(item,0));
  }

  QString s;
  QXmlStreamWriter* str = toHtmlStart(&s,title,div,idx);
  toHtmlInternal(str,idx);

  toHtmlEnd(str,div);
  return s;
}
