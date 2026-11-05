#include "MainWindow.hpp"
#include "ProjectHistory.hpp"
#include "ProjectStore.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString display(const QJsonValue& value){
  if(value.isUndefined())return QStringLiteral("—");
  if(value.isString())return value.toString();
  const auto bytes=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
  return QString::fromUtf8(bytes.mid(1,bytes.size()-2));
}
}

void MainWindow::showProjectHistory(){
  if(parsing_||generationRunning_){
    QMessageBox::information(this,tr("프로젝트 버전 이력"),tr("해석과 문서 생성이 끝난 뒤 열어 주세요."));
    return;
  }
  if(currentProjectPath_.isEmpty())saveProject();
  if(currentProjectPath_.isEmpty())return;
  const auto path=currentProjectPath_;
  const auto current=ProjectStore::serialize(path,captureProjectState());
  const ProjectHistory history(path);

  QDialog dialog(this);
  dialog.setObjectName("projectHistoryDialog");
  dialog.setWindowTitle(tr("프로젝트 버전 이력"));
  dialog.resize(950,530);
  auto* layout=new QVBoxLayout(&dialog);
  auto* selectors=new QFormLayout;
  auto* older=new QComboBox(&dialog);
  auto* newer=new QComboBox(&dialog);
  older->setObjectName("historyBaseVersion");
  newer->setObjectName("historyTargetVersion");
  selectors->addRow(tr("기준 버전"),older);
  selectors->addRow(tr("비교 대상"),newer);
  layout->addLayout(selectors);
  auto* table=new QTableWidget(0,4,&dialog);
  table->setObjectName("projectChanges");
  table->setHorizontalHeaderLabels({tr("분류"),tr("항목"),tr("기준 값"),tr("비교 값")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(table);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
  auto* record=buttons->addButton(tr("현재 상태 버전 저장…"),QDialogButtonBox::ActionRole);
  auto* restore=buttons->addButton(tr("기준 버전 복원…"),QDialogButtonBox::ActionRole);
  record->setObjectName("recordProjectVersion");
  restore->setObjectName("restoreProjectVersion");
  layout->addWidget(buttons);

  auto refresh=[&]{
    table->setRowCount(0);
    restore->setEnabled(older->currentIndex()>=0);
    if(older->currentIndex()<0)return;
    QJsonObject base,target=current;
    QString error;
    if(!history.read(older->currentData().toString(),&base,&error)||
       (!newer->currentData().toString().isEmpty()&&!history.read(newer->currentData().toString(),&target,&error))){
      QMessageBox::warning(&dialog,tr("버전 읽기 실패"),error);
      return;
    }
    const auto changes=ProjectHistory::compare(base,target);
    table->setRowCount(changes.size());
    for(int row=0;row<changes.size();++row){
      const auto& change=changes[row];
      const QStringList values={change.category,change.item,display(change.before),display(change.after)};
      for(int col=0;col<values.size();++col){
        auto* item=new QTableWidgetItem(values[col]);
        item->setToolTip(values[col].toHtmlEscaped());
        table->setItem(row,col,item);
      }
    }
  };
  auto reload=[&]{
    QVector<ProjectVersion> versions;
    QString error;
    if(!history.list(&versions,&error)){
      QMessageBox::warning(&dialog,tr("버전 목록 읽기 실패"),error);
      return;
    }
    {
      const QSignalBlocker a(older),b(newer);
      older->clear();newer->clear();
      newer->addItem(tr("현재 작업 상태"),QString());
      for(const auto& version:versions){
        const auto label=version.created.toLocalTime().toString("yyyy-MM-dd HH:mm:ss.zzz")+"  "+version.message;
        older->addItem(label,version.id);newer->addItem(label,version.id);
      }
    }
    refresh();
  };
  connect(older,&QComboBox::currentIndexChanged,&dialog,[&]{refresh();});
  connect(newer,&QComboBox::currentIndexChanged,&dialog,[&]{refresh();});
  connect(record,&QPushButton::clicked,&dialog,[&]{
    bool accepted=false;
    const auto message=QInputDialog::getText(&dialog,tr("버전 저장"),tr("변경 설명"),QLineEdit::Normal,{},&accepted).trimmed();
    if(!accepted||message.isEmpty())return;
    QString error;
    if(history.record(current,message,&error))reload();
    else QMessageBox::warning(&dialog,tr("버전 저장 실패"),error);
  });
  connect(restore,&QPushButton::clicked,&dialog,[&]{
    if(older->currentIndex()<0||QMessageBox::question(&dialog,tr("버전 복원"),
      tr("기준 버전을 프로젝트에 복원할까요? 현재 작업 상태는 복원 직전 버전으로 저장됩니다."))!=QMessageBox::Yes)return;
    ProjectState state;
    QString error;
    if(!history.restore(older->currentData().toString(),current,&error)||!ProjectStore::load(path,&state,&error)){
      QMessageBox::warning(&dialog,tr("버전 복원 실패"),error);
      return;
    }
    applyProjectState(path,state);
    dialog.accept();
  });
  connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  reload();
  dialog.exec();
}
