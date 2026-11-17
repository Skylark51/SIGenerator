#include "MainWindow.hpp"
#include "MoleculeViewWidget.hpp"
#include "StructureAssignmentTable.hpp"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <algorithm>

QString MainWindow::atomGeometryHash(const si::GaussianResult& result){
  QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  for(const auto& atom:result.finalGeometry){stream<<atom.atomicNumber;for(double v:atom.xyz)stream<<v;}
  return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}

std::shared_ptr<const si::GaussianResult> MainWindow::orderedGaussian(const QString& path) const {
  const auto raw=parsed_.value(path);if(!raw)return raw;
  const auto saved=atomOrderings_.value(path).toObject();
  if(saved.isEmpty()||saved.value("geometry_hash").toString()!=atomGeometryHash(*raw))return raw;
  std::vector<int> permutation;
  for(const auto& value:saved.value("indices").toArray()){
    if(!value.isDouble()||value.toDouble()!=value.toInt(-1))return raw;
    permutation.push_back(value.toInt(-1));
  }
  auto ordered=std::make_shared<si::GaussianResult>(*raw);
  return si::applyAtomOrdering(*ordered,permutation)?ordered:raw;
}

void MainWindow::editAtomOrdering(){
  if(parsing_||!stageGrid_||stageGrid_->rowCount()==0){status_->setText(tr("구조를 불러온 뒤 원자 순서를 통일하세요."));return;}
  QStringList scope;for(int i=0;i<stageGrid_->rowCount();++i)scope.append(stageGrid_->item(i,0)->data(Qt::UserRole).toString());
  QDialog dialog(this);dialog.setObjectName("atomOrderingDialog");dialog.setWindowTitle(tr("원자 순서 자동 통일"));dialog.resize(820,550);
  auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
  auto* reference=new QComboBox(&dialog);reference->setObjectName("atomOrderingReference");
  const auto paths=sourcePaths(),labels=structureLabels();
  for(int i=0;i<paths.size();++i)reference->addItem(labels.value(i),paths[i]);
  const auto referencePath=activeStage_>=0?stageReferencePath(reactionStages_[activeStage_].toObject(),paths):reference_->currentData().toString();
  reference->setCurrentIndex(std::max(0,reference->findData(referencePath)));form->addRow(tr("원자 번호 기준 구조"),reference);
  auto* limit=new QDoubleSpinBox(&dialog);limit->setObjectName("atomOrderingRmsd");limit->setRange(0.02,1.5);limit->setDecimals(2);limit->setValue(0.75);limit->setSuffix(QStringLiteral(" Å"));form->addRow(tr("최대 정렬 RMSD"),limit);
  auto* note=new QLabel(tr("현재 목록 %1개에 적용하며, 다른 반응에서 공유하는 구조도 같은 번호를 사용합니다. 원소별 거리와 회전·이동을 제거한 겹침으로 원자를 일대일 대응합니다. 원소 조성이 다르거나 허용 편차를 넘으면 건너뜁니다. 대칭 원자·큰 구조 변화의 화학적 대응은 RMSD만으로 보장되지 않으므로 결과를 검토하세요.\n좌표와 스핀 번호에 함께 적용하며, 좌표값과 원본 파일은 바꾸지 않습니다. 공통 기하·스핀 원자 번호는 통일된 번호로 해석됩니다. 구조별 스핀 지정과 Figure의 표시 수소는 같은 원자를 유지합니다.").arg(scope.size()),&dialog);note->setWordWrap(true);layout->addWidget(note);
  auto* preview=new QPushButton(tr("자동 대응 계산"),&dialog);preview->setObjectName("previewAtomOrdering");layout->addWidget(preview);
  auto* table=new QTableWidget(0,4,&dialog);table->setObjectName("atomOrderingResults");table->setHorizontalHeaderLabels({tr("구조"),tr("결과"),tr("RMSD (Å)"),tr("번호 변경 원자 수")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(table,1);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
  auto* apply=buttons->button(QDialogButtonBox::Apply);apply->setObjectName("applyAtomOrdering");apply->setEnabled(false);
  auto* restore=buttons->addButton(tr("현재 목록 원본 순서 복원"),QDialogButtonBox::ResetRole);restore->setObjectName("restoreAtomOrdering");
  QJsonObject planned;bool restoreOriginal=false;
  auto invalidate=[&]{planned={};table->setRowCount(0);apply->setEnabled(false);};
  connect(reference,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,invalidate);
  connect(limit,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,invalidate);
  connect(preview,&QPushButton::clicked,&dialog,[&]{
    invalidate();const auto ref=orderedGaussian(reference->currentData().toString());if(!ref)return;
    QProgressDialog progress(tr("원자 대응 계산 중…"),tr("취소"),0,scope.size(),&dialog);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(300);
    for(int row=0;row<scope.size();++row){
      progress.setValue(row);if(progress.wasCanceled()){invalidate();return;}
      const auto path=scope[row];const auto raw=parsed_.value(path);QString outcome;QString rmsd=QStringLiteral("—"),changed=QStringLiteral("—"),detail;
      if(path==reference->currentData().toString())outcome=tr("기준 구조 (유지)");
      else if(raw){
        const auto match=si::matchAtomOrdering(ref->finalGeometry,raw->finalGeometry,limit->value());
        if(match.accepted){
          auto checked=*raw;
          if(si::applyAtomOrdering(checked,match.sourceIndices)){
            QJsonArray indices;int moved=0;QStringList mapping;
            for(size_t i=0;i<match.sourceIndices.size();++i){const int old=match.sourceIndices[i];indices.append(old);moved+=old!=static_cast<int>(i);mapping.append(QStringLiteral("%1 ← %2").arg(i+1).arg(old+1));}
            planned.insert(path,QJsonObject{{"geometry_hash",atomGeometryHash(*raw)},{"indices",indices}});
            outcome=moved?tr("통일 가능"):tr("이미 동일");changed=QString::number(moved);rmsd=QString::number(match.rmsd,'f',4);detail=mapping.join(", ");
          }else outcome=tr("건너뜀: 스핀 원자 번호 오류");
        }else{
          outcome=tr("건너뜀: 대응 검토 필요");
          if(match.reason.find("counts")!=std::string::npos)outcome=tr("건너뜀: 원자 수 다름/좌표 없음");
          else if(match.reason.find("compositions")!=std::string::npos)outcome=tr("건너뜀: 원소 조성 다름");
          else if(match.reason.find("RMSD exceeds")!=std::string::npos)outcome=tr("건너뜀: RMSD 초과");
          detail=QString::fromStdString(match.reason);if(match.rmsd>=0)rmsd=QString::number(match.rmsd,'f',4);
        }
      }else outcome=tr("건너뜀: 추출 데이터 없음");
      table->insertRow(row);const QStringList cells{labels.value(paths.indexOf(path)),outcome,rmsd,changed};
      for(int col=0;col<4;++col){auto* item=new QTableWidgetItem(cells[col]);item->setToolTip(detail);table->setItem(row,col,item);}
    }
    progress.setValue(scope.size());apply->setEnabled(!planned.isEmpty());
  });
  connect(apply,&QPushButton::clicked,&dialog,&QDialog::accept);
  connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  connect(restore,&QPushButton::clicked,&dialog,[&]{restoreOriginal=true;dialog.accept();});
  if(dialog.exec()!=QDialog::Accepted)return;
  const auto previous=atomOrderings_;
  if(restoreOriginal)for(const auto& path:scope)atomOrderings_.remove(path);
  else for(auto it=planned.begin();it!=planned.end();++it)atomOrderings_.insert(it.key(),it.value());
  // Per-structure spin selections and retained hydrogens refer to physical
  // atoms. Common spin/geometry IDs intentionally use the reference numbering.
  auto spinRows=spinIndices_->toPlainText().split(QRegularExpression("[\\r\\n|]+"),Qt::SkipEmptyParts);
  const bool perStructureSpins=spinRows.size()>1&&spinRows.size()==paths.size();
  for(const auto& path:scope){
    const auto raw=parsed_.value(path);if(!raw)continue;const int n=static_cast<int>(raw->finalGeometry.size());
    auto permutation=[&](const QJsonObject& mappings){
      std::vector<int> indices;const auto saved=mappings.value(path).toObject();
      if(saved.value("geometry_hash").toString()==atomGeometryHash(*raw))for(const auto& v:saved.value("indices").toArray())indices.push_back(v.toInt(-1));
      auto check=*raw;if(!si::applyAtomOrdering(check,indices)){indices.resize(n);for(int i=0;i<n;++i)indices[i]=i;}return indices;
    };
    const auto before=permutation(previous),after=permutation(atomOrderings_);if(before==after)continue;
    std::vector<int> inverse(n);for(int i=0;i<n;++i)inverse[after[i]]=i;
    auto remap=[&](int id){return id>0&&id<=n?inverse[before[id-1]]+1:id;};
    auto updateFigure=[&](QJsonObject settings){auto retained=settings.value("retained_hydrogens").toObject();if(retained.contains(path)){retained[path]=remap(retained.value(path).toInt());settings["retained_hydrogens"]=retained;}return settings;};
    figureSettings_=updateFigure(figureSettings_);
    for(int i=0;i<reactionStages_.size();++i){auto stage=reactionStages_[i].toObject();if(stage.contains("figure_settings")){stage["figure_settings"]=updateFigure(stage["figure_settings"].toObject());reactionStages_[i]=stage;}}
    if(perStructureSpins){const int row=paths.indexOf(path);QStringList updated;for(const auto& token:spinRows[row].split(QRegularExpression("[^0-9]+"),Qt::SkipEmptyParts))updated.append(QString::number(remap(token.toInt())));spinRows[row]=updated.join(' ');}
  }
  if(perStructureSpins)spinIndices_->setPlainText(spinRows.join('\n'));
  preparedFigures_.clear();preparedStageFigures_.clear();figureSignature_.clear();
  selectStructure();refreshDocumentModel();
  status_->setText(restoreOriginal?tr("현재 목록의 원본 원자 순서를 복원했습니다."):tr("%1개 구조의 원자 순서를 적용했습니다. 원본 파일은 변경하지 않았습니다.").arg(planned.size()));
}
