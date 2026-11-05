#pragma once
#include "si/StructureLabels.hpp"
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextDocument>
#include <functional>

class StructureNameDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
    QStyleOptionViewItem opt(option);initStyleOption(&opt,index);
    const auto text=opt.text;auto* style=opt.widget?opt.widget->style():QApplication::style();
    const auto rect=style->subElementRect(QStyle::SE_ItemViewItemText,&opt,opt.widget);
    opt.text.clear();style->drawControl(QStyle::CE_ItemViewItem,&opt,painter,opt.widget);
    QTextDocument doc;doc.setDocumentMargin(0);doc.setDefaultFont(opt.font);
    const bool focusedSelection=(opt.state&QStyle::State_Selected)&&opt.widget&&opt.widget->hasFocus();
    const auto color=opt.palette.color(focusedSelection?QPalette::HighlightedText:QPalette::Text);
    doc.setHtml("<span style='white-space:nowrap;color:"+color.name()+"'>"+si::formattedTextHtml(si::structureLabelMarkup(text))+"</span>");
    painter->save();painter->setClipRect(rect);painter->translate(rect.left()+3,rect.top()+std::max(0.0,(rect.height()-doc.size().height())/2));doc.drawContents(painter);painter->restore();
  }
  QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index) const override {auto size=QStyledItemDelegate::sizeHint(option,index);size.setHeight(std::max(32,size.height()));return size;}
};

class StructureAssignmentTable final : public QTableWidget {
public:
  explicit StructureAssignmentTable(QWidget* parent=nullptr,bool showMetadata=false):QTableWidget(0,showMetadata?7:2,parent){
    setObjectName("reactionStageMembers");
    if(showMetadata){
      setHorizontalHeaderLabels({tr("구조명"),tr("Output 파일"),tr("M"),tr("Freq"),tr("SPE"),tr("Imag"),tr("상태")});
      horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
      horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
      for(int column=2;column<7;++column)horizontalHeader()->setSectionResizeMode(column,QHeaderView::ResizeToContents);
      setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }else{
      setHorizontalHeaderLabels({tr("구조명"),tr("Output 파일")});
      horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }
    verticalHeader()->setDefaultSectionSize(34);
    setItemDelegateForColumn(0,new StructureNameDelegate(this));setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);setMinimumHeight(200);
  }
  std::function<void(const QString&)> pasteText;
protected:
  void keyPressEvent(QKeyEvent* event) override {
    if(event->matches(QKeySequence::Paste)&&pasteText){pasteText(QApplication::clipboard()->text());event->accept();return;}
    if(event->matches(QKeySequence::Copy)){
      QStringList rows;const auto ranges=selectedRanges();if(!ranges.isEmpty()){const auto range=ranges.first();for(int row=range.topRow();row<=range.bottomRow();++row){QStringList cells;for(int col=range.leftColumn();col<=range.rightColumn();++col)cells.append(item(row,col)?item(row,col)->text():QString());rows.append(cells.join('\t'));}QApplication::clipboard()->setText(rows.join('\n'));}event->accept();return;
    }
    QTableWidget::keyPressEvent(event);
  }
};
