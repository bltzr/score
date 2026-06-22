#include "RecursiveFilterProxy.hpp"

#include "ProcessesItemModel.hpp"

#include <Process/ProcessList.hpp>

#include <score/application/GUIApplicationContext.hpp>

namespace Library
{

const QString& RecursiveFilterProxy::pattern() const noexcept
{
  return m_textPattern;
}

void RecursiveFilterProxy::setPattern(const QString& p)
{
  beginResetModel();
  // auto old = m_textPattern;
  m_textPattern = p;
  endResetModel();
  // if(!p.contains(old))
  //  invalidateFilter();
}

int RecursiveFilterProxy::columnCount(const QModelIndex& parent) const
{
  return 1;
}

bool RecursiveFilterProxy::filterAcceptsRow(
    int srcRow, const QModelIndex& srcParent) const
{
  if(filterAcceptsRowItself(srcRow, srcParent))
  {
    return true;
  }

  // Accept if any of the parents is accepted on its own
  for(QModelIndex parent = srcParent; parent.isValid(); parent = parent.parent())
    if(filterAcceptsRowItself(parent.row(), parent.parent()))
    {
      return true;
    }

  // Accept if any of the children is accepted on its own
  return hasAcceptedChildren(srcRow, srcParent);
}

bool RecursiveFilterProxy::hasAcceptedChildren(
    int srcRow, const QModelIndex& srcParent) const
{
  QModelIndex index = sourceModel()->index(srcRow, 0, srcParent);

  if(!index.isValid())
    return false;

  SCORE_ASSERT(index.model());
  const int childCount = index.model()->rowCount(index);

  if(childCount == 0)
    return false;

  for(int i = 0; i < childCount; ++i)
  {
    if(filterAcceptsRowItself(i, index))
      return true;

    if(hasAcceptedChildren(i, index))
      return true;
  }

  return false;
}

bool ProcessFilterProxy::filterAcceptsRowItself(
    int srcRow, const QModelIndex& srcParent) const
{
  QModelIndex index = sourceModel()->index(srcRow, 0, srcParent);
  auto model = static_cast<ProcessesItemModel*>(sourceModel());
  auto& node = model->nodeFromModelIndex(index);

  if(node.prettyName.contains(m_textPattern, Qt::CaseInsensitive))
    return true;

  // Also match against the process tags, if any.
  // The factory lookup / descriptor() call only happens when the (cheap) name
  // check above fails, to keep filtering on large libraries reasonably fast.
  if(auto f = score::GUIAppContext()
                  .interfaces<Process::ProcessFactoryList>()
                  .get(node.key))
  {
    for(const QString& tag : f->descriptor(node.customData).tags)
      if(tag.contains(m_textPattern, Qt::CaseInsensitive))
        return true;
  }

  return false;
}
}
