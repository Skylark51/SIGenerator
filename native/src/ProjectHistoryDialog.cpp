#include "MainWindow.hpp"
#include "ProjectHistory.hpp"
#include "ProjectStore.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QJsonArray>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString display(const QJsonValue& value){
  if(value.isUndefined()||value.isNull())return QStringLiteral("—");
  if(value.isBool())return QObject::tr(value.toBool()?"사용":"사용 안 함");
  if(value.isDouble())return QString::number(value.toDouble(),'g',15);
  if(value.isString())return value.toString();
  if(value.isArray())return QObject::tr("%1개 항목").arg(value.toArray().size());
  return QObject::tr("설정 있음");
}
QString describeItem(const ProjectChange& change){
  static const QHash<QString,QString> labels{
    {"Order",QObject::tr("순서")},{"name",QObject::tr("이름")},{"files",QObject::tr("포함된 구조")},
    {"reference",QObject::tr("기준 구조")},{"absolute",QObject::tr("절대 에너지 표")},
    {"relative",QObject::tr("상대 에너지 표")},{"reference_settings",QObject::tr("기준 구조와 에너지 기준")},
    {"manual_energy_tables",QObject::tr("수동 에너지 표")},{"energy_input_mode",QObject::tr("에너지 입력 방식")},
    {"molecular_figures",QObject::tr("구성·카메라·캡션")},{"figure_settings",QObject::tr("구성·카메라·캡션")},
    {"figure_captions",QObject::tr("캡션")},{"captions",QObject::tr("캡션")},
    {"include_absolute_table",QObject::tr("절대 에너지 표")},{"include_relative_dispersion",QObject::tr("분산 보정")},
    {"include_relative_complexation",QObject::tr("착물화 보정")},{"computational_details",QObject::tr("Computational Details")},
    {"document_title",QObject::tr("문서 제목")},{"output_paths",QObject::tr("저장 경로")},
    {"references_text",QObject::tr("참고문헌")},{"manual_paired_paths",QObject::tr("수동 SPE 연결")},
    {"paired_snapshot_paths",QObject::tr("SPE 연결")},{"project_name",QObject::tr("프로젝트 이름")},
    {"mechanism_name",QObject::tr("메커니즘")},{"manuscript_metadata",QObject::tr("원고 정보")},
    {"spin_settings",QObject::tr("스핀 밀도 표")},{"geometry_settings",QObject::tr("선택 구조 표")}};
  const auto parts=change.item.split(" / ");
  const auto key=parts.last();
  QString label=labels.value(key,key.contains('/')?QFileInfo(key).fileName():QString(key).replace('_',' '));
  if(parts.size()>1){
    const auto scope=parts.first();
    label=(scope.contains('/')?QFileInfo(scope).fileName():scope)+" / "+label;
  }
  return label;
}
QString describeChange(const ProjectChange& change){
  if(change.category=="Structure added")return QObject::tr("구조 추가");
  if(change.category=="Structure removed")return QObject::tr("구조 삭제");
  if(change.category=="Structure renamed")return QObject::tr("구조 이름 변경");
  if(change.category=="Structures")return QObject::tr("구조 순서 변경");
  if(change.category=="Energy")return QObject::tr("에너지 변경");
  if(change.category=="Figure")return QObject::tr("Figure 수정");
  if(change.category=="Gaussian data")return QObject::tr("Gaussian 추출 데이터 갱신");
  if(change.category=="Reaction stage added")return QObject::tr("반응 단계 추가");
  if(change.category=="Reaction stage removed")return QObject::tr("반응 단계 삭제");
  if(change.category=="Reaction stage")return QObject::tr("반응 단계 수정");
  if(change.category=="SPE")return QObject::tr("SPE 연결 변경");
  return QObject::tr("프로젝트 설정 변경");
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
  table->setHorizontalHeaderLabels({tr("변경 내역"),tr("항목"),tr("이전"),tr("이후")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(table);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
  auto* record=buttons->addButton(tr("현재 상태 버전 저장…"),QDialogButtonBox::ActionRole);
  auto* restore=buttons->addButton(tr("기준 버전 복원…"),QDialogButtonBox::ActionRole);
  record->setObjectName("recordProjectVersion");
  restore->setObjectName("restoreProjectVersion");
  auto* compareDocx=buttons->addButton(tr("보관한 DOCX 두 버전 비교…"),QDialogButtonBox::ActionRole);
  auto* archiveInfo=new QLabel(&dialog);layout->addWidget(archiveInfo);
  layout->addWidget(buttons);

  auto refresh=[&]{
    table->setRowCount(0);
    const auto olderDocx=older->currentData(Qt::UserRole+1).toString();
    const auto newerDocx=newer->currentData(Qt::UserRole+1).toString();
    compareDocx->setEnabled(false);
#ifdef Q_OS_WIN
    compareDocx->setEnabled(!olderDocx.isEmpty()&&!newerDocx.isEmpty()&&olderDocx!=newerDocx);
#endif
    archiveInfo->setText(tr("보관 DOCX가 있는 서로 다른 두 버전을 선택하면 Word에서 비교할 수 있습니다."));
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
      const bool summary=change.category=="Gaussian data"||change.before.isObject()||change.after.isObject()||
        change.before.isArray()||change.after.isArray();
      const QStringList values={describeChange(change),describeItem(change),
        summary&&!change.before.isUndefined()?tr("이전 상태"):display(change.before),
        summary&&!change.after.isUndefined()?tr("변경됨"):display(change.after)};
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
        const auto label=version.created.toLocalTime().toString("yyyy-MM-dd HH:mm:ss.zzz")+"  "+version.message+
          (version.docxPath.isEmpty()?QString():tr("  [DOCX]"));
        older->addItem(label,version.id);newer->addItem(label,version.id);
        older->setItemData(older->count()-1,version.docxPath,Qt::UserRole+1);
        newer->setItemData(newer->count()-1,version.docxPath,Qt::UserRole+1);
      }
    }
    refresh();
  };
  connect(older,&QComboBox::currentIndexChanged,&dialog,[&]{refresh();});
  connect(newer,&QComboBox::currentIndexChanged,&dialog,[&]{refresh();});
  connect(compareDocx,&QPushButton::clicked,&dialog,[&]{
    runWordOperation(this,older->currentData(Qt::UserRole+1).toString(),newer->currentData(Qt::UserRole+1).toString());
  });
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
