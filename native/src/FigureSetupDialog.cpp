#include "FigureSetupDialog.hpp"
#include "MoleculeViewWidget.hpp"
#include "MolecularStyle.hpp"
#include "si/StructureLabels.hpp"
#include <QTextDocument>
#include <QTextOption>
#include <QtMath>
#include <memory>
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QFontMetrics>
#include <QPushButton>
#include <QSignalBlocker>
#include <QQuaternion>
#include <QVector3D>
#include <QVBoxLayout>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace {
// Same proper-rotation Horn fit and heavy-atom policy as Youngcraft capture.
bool alignTo(std::vector<si::Atom>& moving, const std::vector<si::Atom>& target) {
    if (moving.empty() || moving.size() != target.size()) return false;
    QVector3D a, b;
    int count = 0;
    for (int i=0; i<moving.size(); ++i) {
        if (moving[i].atomicNumber != target[i].atomicNumber) return false;
        if (target[i].atomicNumber == 1) continue;
        a += QVector3D(moving[i].xyz[0],moving[i].xyz[1],moving[i].xyz[2]); b += QVector3D(target[i].xyz[0],target[i].xyz[1],target[i].xyz[2]); ++count;
    }
    const bool all = count < 3;
    if (all) {
        a={};b={};count=0;
        for (int i=0; i<moving.size(); ++i) {
            a += QVector3D(moving[i].xyz[0],moving[i].xyz[1],moving[i].xyz[2]); b += QVector3D(target[i].xyz[0],target[i].xyz[1],target[i].xyz[2]); ++count;
        }
    }
    a /= float(count); b /= float(count);
    double s[3][3] = {};
    for (int i=0; i<moving.size(); ++i) {
        if (!all && target[i].atomicNumber == 1) continue;
        auto x = QVector3D(moving[i].xyz[0],moving[i].xyz[1],moving[i].xyz[2])-a, y = QVector3D(target[i].xyz[0],target[i].xyz[1],target[i].xyz[2])-b;
        for (int j=0;j<3;++j) for(int k=0;k<3;++k) s[j][k] += double(x[j])*y[k];
    }
    const auto &sxx=s[0][0], &sxy=s[0][1], &sxz=s[0][2], &syx=s[1][0], &syy=s[1][1], &syz=s[1][2], &szx=s[2][0], &szy=s[2][1], &szz=s[2][2];
    double n[4][4] = {{sxx+syy+szz, syz-szy, szx-sxz, sxy-syx},
        {syz-szy,sxx-syy-szz,sxy+syx,szx+sxz},
        {szx-sxz,sxy+syx,-sxx+syy-szz,syz+szy},
        {sxy-syx,szx+sxz,syz+szy,-sxx-syy+szz}};
    double v[4][4] = {}; for(int i=0;i<4;++i) v[i][i]=1;
    for(int iter=0;iter<64;++iter) {
        int p=0,q=1;
        for(int i=0;i<4;++i) for(int j=i+1;j<4;++j) if(std::abs(n[i][j])>std::abs(n[p][q])) {p=i;q=j;}
        if(std::abs(n[p][q])<1e-12) break;
        const double angle=0.5*std::atan2(2*n[p][q],n[q][q]-n[p][p]);
        const double c=std::cos(angle), t=std::sin(angle);
        const double pp=n[p][p],qq=n[q][q],pq=n[p][q];
        for(int k=0;k<4;++k) if(k!=p && k!=q) {
            const double kp=n[k][p],kq=n[k][q];
            n[k][p]=n[p][k]=c*kp-t*kq; n[k][q]=n[q][k]=t*kp+c*kq;
        }
        n[p][p]=c*c*pp-2*c*t*pq+t*t*qq; n[q][q]=t*t*pp+2*c*t*pq+c*c*qq;
        n[p][q]=n[q][p]=0;
        for(int k=0;k<4;++k) {const double kp=v[k][p],kq=v[k][q];v[k][p]=c*kp-t*kq;v[k][q]=t*kp+c*kq;}
    }
    int best=0;for(int i=1;i<4;++i) if(n[i][i]>n[best][best]) best=i;
    QQuaternion rotation{float(v[0][best]),float(v[1][best]),float(v[2][best]),float(v[3][best])};
    rotation.normalize();
    for(auto& atom:moving) {
        const auto point=b+rotation.rotatedVector(QVector3D(atom.xyz[0],atom.xyz[1],atom.xyz[2])-a);
        atom.xyz={double(point.x()),double(point.y()),double(point.z())};
    }
    return true;
}
}
FigureSetupDialog::FigureSetupDialog(const QVector<si::CoordinateBlock>& structures,const QStringList& paths,const QJsonObject& settings,QWidget* parent)
  : QDialog(parent),structures_(structures),paths_(paths),views_(settings.value("views").toObject()) {
  renderStyle_=currentYoungcraftStyle();
  const auto storedHydrogens=settings.value("retained_hydrogens").toObject();
  for(auto it=storedHydrogens.begin();it!=storedHydrogens.end();++it)
    if(paths_.contains(it.key()))retainedHydrogens_.insert(it.key(),it.value().toInt());
  setObjectName("figureSetupDialog");setWindowTitle(tr("SI 분자 Figure 준비"));resize(1250,650);
  QSet<int> used;QJsonArray sourceSequence;for(const auto& path:paths_)sourceSequence.append(path);
  // Version 1 saved an order produced by a filename substring heuristic.
  // Restore deliberate panel edits only for this exact, unchanged input list.
  if(settings.value("sequence_version").toInt()==2&&settings.value("sources").toArray()==sourceSequence)
    for(const auto& value:settings.value("order").toArray()){
      const int index=paths_.indexOf(value.toString());if(index>=0&&!used.contains(index)){order_.append(index);used.insert(index);}
    }
  for(int i=0;i<structures_.size();++i)if(!used.contains(i))order_.append(i);
  rebuildDefaultRows();
  if(settings.value("rows_version").toInt()==1&&settings.value("sources").toArray()==sourceSequence){
    QVector<QVector<int>> restored;
    for(const auto& entry:settings.value("rows").toArray()){
      if(!entry.isArray())continue;
      QVector<int> row;
      for(const auto& path:entry.toArray()){
        const int index=paths_.indexOf(path.toString());
        if(index>=0)row.append(index);
      }
      if(!row.isEmpty())restored.append(row);
    }
    if(!restored.isEmpty()){
      QVector<QVector<int>> limited;
      QJsonObject updatedViews=views_;
      for(int oldRow=0;oldRow<restored.size();++oldRow){
        const auto& row=restored[oldRow];
        for(int first=0;first<row.size();first+=3){
          const int newRow=limited.size();
          QVector<int> chunk;
          for(int original=first;original<std::min(first+3,int(row.size()));++original){
            chunk.append(row[original]);
            const auto oldKey=QStringLiteral("row:%1:%2").arg(oldRow).arg(original);
            const auto newKey=QStringLiteral("row:%1:%2").arg(newRow).arg(original-first);
            if(views_.contains(oldKey))updatedViews.insert(newKey,views_.value(oldKey));
          }
          limited.append(chunk);
        }
      }
      rows_=limited;views_=updatedViews;customRows_=true;
    }
  }
  auto* layout=new QVBoxLayout(this);
  auto* instruction=new QLabel(tr("렌더링 한 줄마다 원하는 구조를 선택하세요. 기본 구성은 첫 줄 3개, 이후 2개입니다.\n[새 줄]·[현재 줄에 추가]로 줄을 자유롭게 구성하며 같은 구조도 여러 줄에 사용할 수 있습니다. Youngcraft 스타일과 카메라 설정은 유지됩니다."),this);
  instruction->setWordWrap(true);layout->addWidget(instruction);
  auto* paletteLabel=new QLabel(tr("Youngcraft 저장 스타일: %1").arg(renderStyle_.value("name").toString()),this);paletteLabel->setObjectName("youngcraftStyleName");layout->addWidget(paletteLabel);
  auto* controls=new QHBoxLayout;
  linked_=new QCheckBox(tr("카메라 함께 회전"),this);linked_->setObjectName("linkFigureCameras");linked_->setChecked(true);controls->addWidget(linked_);
  indices_=new QCheckBox(tr("원자 번호 표시"),this);indices_->setChecked(settings.value("indices").toBool(false));controls->addWidget(indices_);
  hideHydrogens_=new QCheckBox(tr("수소 원자 숨기기"),this);
  hideHydrogens_->setObjectName("hideFigureHydrogens");
  hideHydrogens_->setToolTip(tr("모든 수소를 숨기되 구조별로 선택한 C–H 활성화 대상 수소는 표시합니다."));
  hideHydrogens_->setChecked(settings.value("hide_hydrogens").toBool(false));
  controls->addWidget(hideHydrogens_);
  auto* match=new QPushButton(tr("TS 기준 정렬 및 뷰 맞추기"),this);match->setObjectName("alignFigureViews");controls->addWidget(match);
  auto* reset=new QPushButton(tr("뷰 초기화"),this);controls->addWidget(reset);layout->addLayout(controls);
  auto* lineControls=new QHBoxLayout;
  auto selectSource=[this](const QString& title)->int{
    QStringList choices;
    for(int index=0;index<structures_.size();++index)
      choices.append(QStringLiteral("%1  [%2: %3]").arg(structures_[index].structureName).arg(index+1).arg(QFileInfo(paths_.value(index)).fileName()));
    bool accepted=false;
    const QString name=QInputDialog::getItem(this,title,tr("구조명"),choices,0,false,&accepted);
    return accepted?choices.indexOf(name):-1;
  };
  auto* addPanel=new QPushButton(tr("현재 줄에 구조 추가…"),this);
  addPanel->setObjectName("addFigurePanel");addPanel_=addPanel;lineControls->addWidget(addPanel);
  connect(addPanel,&QPushButton::clicked,this,[this,selectSource]{
    if(rows_.isEmpty()||rows_[group_].size()>=3){status_->setText(tr("한 줄에는 최대 3개까지 표시할 수 있습니다."));return;}
    const int chosen=selectSource(tr("현재 Figure 줄에 추가할 구조"));if(chosen<0)return;
    storeGroup();customRows_=true;rows_[group_].append(chosen);showGroup();
  });
  auto* newLine=new QPushButton(tr("새 렌더링 줄 추가…"),this);
  newLine->setObjectName("appendFigureRow");lineControls->addWidget(newLine);
  connect(newLine,&QPushButton::clicked,this,[this,selectSource]{
    const int chosen=selectSource(tr("새 Figure 줄의 첫 구조"));if(chosen<0)return;
    storeGroup();customRows_=true;rows_.append(QVector<int>{chosen});group_=rows_.size()-1;showGroup();
  });
  auto* deleteLine=new QPushButton(tr("현재 줄 삭제"),this);
  deleteLine->setObjectName("removeFigureRow");lineControls->addWidget(deleteLine);
  connect(deleteLine,&QPushButton::clicked,this,[this]{
    if(rows_.size()<=1){status_->setText(tr("적어도 한 줄은 필요합니다."));return;}
    storeGroup();customRows_=true;rows_.removeAt(group_);group_=std::min(group_,int(rows_.size())-1);showGroup();
  });
  lineControls->addStretch();layout->addLayout(lineControls);
  panelWidget_=new QWidget(this);panels_=new QVBoxLayout(panelWidget_);panels_->setContentsMargins(0,0,0,0);layout->addWidget(panelWidget_,1);
  status_=new QLabel(this);status_->setWordWrap(true);layout->addWidget(status_);
  auto* buttons=new QDialogButtonBox(this);
  previous_=buttons->addButton(tr("이전"),QDialogButtonBox::ActionRole);previous_->setObjectName("previousFigureGroup");
  next_=buttons->addButton(tr("다음"),QDialogButtonBox::ActionRole);next_->setObjectName("nextFigureGroup");
  auto* cancel=buttons->addButton(QDialogButtonBox::Cancel);layout->addWidget(buttons);
  connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
  connect(previous_,&QPushButton::clicked,this,[this]{storeGroup();--group_;showGroup();});
  connect(next_,&QPushButton::clicked,this,[this]{storeGroup();if(group_+1<rows_.size()){++group_;showGroup();}else finish();});
  connect(indices_,&QCheckBox::toggled,this,[this](bool checked){for(auto* view:viewers_)view->setAtomIndicesVisible(checked);});
  connect(hideHydrogens_,&QCheckBox::toggled,this,[this](bool hidden){
    for(int i=0;i<viewers_.size();++i)
      viewers_[i]->setHydrogenVisibility(hidden,retainedHydrogens_.value(paths_[rows_[group_][i]],0));
    for(auto* selector:hydrogenSelectors_)selector->setEnabled(hidden);
  });
  connect(reset,&QPushButton::clicked,this,[this]{changing_=true;for(auto* view:viewers_)view->resetView();changing_=false;});
  connect(match,&QPushButton::clicked,this,[this]{
    const int reference=group_==0&&viewers_.size()==3?1:0;
    if(viewers_.isEmpty())return;
    const auto camera=viewers_[reference]->cameraState();const auto& target=structures_[rows_[group_][reference]].atoms;
    int failed=0;changing_=true;
    for(int i=0;i<viewers_.size();++i){auto atoms=structures_[rows_[group_][i]].atoms;if(i!=reference&&!alignTo(atoms,target))++failed;viewers_[i]->setMolecule(atoms,{});viewers_[i]->setCameraState(camera);}
    changing_=false;
    status_->setText(failed?tr("원자 수 또는 원소 순서가 다른 구조 %1개는 자동 정렬할 수 없습니다. 카메라 함께 회전을 끄고 직접 뷰를 조정하세요.").arg(failed):tr("TS에 강체 회전 정렬했습니다. 원본 좌표는 보존됩니다."));
  });
  showGroup();
}
void FigureSetupDialog::rebuildDefaultRows(){
  rows_.clear();int start=0;
  while(start<order_.size()){
    const int count=std::min(start==0?3:2,int(order_.size())-start);
    QVector<int> row;for(int i=0;i<count;++i)row.append(order_[start+i]);
    rows_.append(row);start+=count;
  }
}
void FigureSetupDialog::storeGroup(){
  if(group_<0||group_>=rows_.size())return;
  for(int i=0;i<viewers_.size()&&i<rows_[group_].size();++i){
    const auto camera=viewers_[i]->cameraState();
    views_.insert(paths_[rows_[group_][i]],camera);
    if(customRows_)views_.insert(QStringLiteral("row:%1:%2").arg(group_).arg(i),camera);
  }
}
void FigureSetupDialog::showGroup(){
  changing_=true;viewers_.clear();selectors_.clear();hydrogenSelectors_.clear();
  while(auto* item=panels_->takeAt(0)){if(auto* widget=item->widget()){widget->hide();widget->deleteLater();}delete item;}
  auto* rowWidget=new QWidget(panelWidget_);auto* row=new QHBoxLayout(rowWidget);row->setContentsMargins(0,0,0,0);panels_->addWidget(rowWidget);
  const int start=group_==0?0:3+(group_-1)*2;
  const int count=group_<rows_.size()?rows_[group_].size():0;
  const int reference=group_==0&&count==3?1:0;
  int failed=0;
  for(int i=0;i<count;++i){
    auto* column=new QWidget(rowWidget);auto* vertical=new QVBoxLayout(column);row->addWidget(column,1);
    vertical->addWidget(new QLabel(tr("줄 %1 · 구조 %2").arg(group_+1).arg(i+1),column));
    auto* selector=new QComboBox(column);selector->setObjectName(QString("figureStructure%1").arg(i));
    if(customRows_){
      for(int index=0;index<structures_.size();++index)selector->addItem(structures_[index].structureName,index);
      selector->setCurrentIndex(rows_[group_][i]);
    }else{
      for(int index:order_)selector->addItem(structures_[index].structureName,index);
      selector->setCurrentIndex(start+i);
    }
    vertical->addWidget(selector);selectors_.append(selector);
    auto* removePanel=new QPushButton(tr("이 칸 제거"),column);
    removePanel->setObjectName(QString("removeFigurePanel%1").arg(i));
    vertical->addWidget(removePanel);
    connect(removePanel,&QPushButton::clicked,this,[this,i]{
      if(rows_[group_].size()<=1){status_->setText(tr("한 줄에는 최소 한 구조가 필요합니다."));return;}
      storeGroup();customRows_=true;rows_[group_].removeAt(i);showGroup();
    });
    const int index=rows_[group_][i];auto atoms=structures_[index].atoms;
    auto* hydrogenSelector=new QComboBox(column);
    hydrogenSelector->setObjectName(QString("figureHydrogenKeep%1").arg(i));
    hydrogenSelector->setToolTip(tr("원본 Gaussian 파일 기준 원자 번호(1부터 시작). 선택한 H와 연결 결합만 표시됩니다."));
    hydrogenSelector->addItem(tr("표시할 수소 없음"),0);
    for(size_t atom=0;atom<structures_[index].atoms.size();++atom)
      if(structures_[index].atoms[atom].atomicNumber==1)
        hydrogenSelector->addItem(QStringLiteral("H(%1)").arg(atom+1),static_cast<int>(atom)+1);
    hydrogenSelector->setCurrentIndex(std::max(0,hydrogenSelector->findData(retainedHydrogens_.value(paths_[index],0))));
    hydrogenSelector->setEnabled(hideHydrogens_->isChecked());
    vertical->addWidget(new QLabel(tr("표시할 H (C–H 활성화)"),column));
    vertical->addWidget(hydrogenSelector);hydrogenSelectors_.append(hydrogenSelector);
    auto* viewer=new MoleculeViewWidget(column);viewer->setObjectName(QString("figureView%1").arg(i));viewer->setCaptureFraming(true);viewer->setMinimumSize(240,280);vertical->addWidget(viewer,1);
    if(i!=reference&&!alignTo(atoms,structures_[rows_[group_][reference]].atoms))++failed;
    viewer->setRenderStyle(renderStyle_);viewer->setMolecule(atoms,{});viewer->setAtomIndicesVisible(indices_->isChecked());
    viewer->setHydrogenVisibility(hideHydrogens_->isChecked(),retainedHydrogens_.value(paths_[index],0));
    const auto slotKey=QStringLiteral("row:%1:%2").arg(group_).arg(i);
    if(customRows_&&views_.contains(slotKey))viewer->setCameraState(views_.value(slotKey).toObject());
    else if(views_.contains(paths_[index]))viewer->setCameraState(views_.value(paths_[index]).toObject());
    viewers_.append(viewer);
    connect(hydrogenSelector,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,index,i](int){
      if(changing_)return;
      const int retained=hydrogenSelectors_[i]->currentData().toInt();
      retainedHydrogens_.insert(paths_[index],retained);
      for(int panel=0;panel<viewers_.size();++panel)if(rows_[group_][panel]==index){
        viewers_[panel]->setHydrogenVisibility(hideHydrogens_->isChecked(),retained);
        if(panel!=i){
          QSignalBlocker blocked(hydrogenSelectors_[panel]);
          hydrogenSelectors_[panel]->setCurrentIndex(hydrogenSelectors_[panel]->findData(retained));
        }
      }
    });
    connect(selector,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,i,start,selector]{
      if(changing_)return;storeGroup();
      const int chosen=selector->currentData().toInt();
      if(customRows_)rows_[group_][i]=chosen;
      else {
        const int other=order_.indexOf(chosen);
        if(other>=0){std::swap(order_[start+i],order_[other]);rebuildDefaultRows();}
      }
      showGroup();
    });
    connect(viewer,&MoleculeViewWidget::cameraChanged,this,[this,viewer]{
      if(changing_||!linked_->isChecked())return;changing_=true;for(auto* other:viewers_)if(other!=viewer)other->setCameraState(viewer->cameraState());changing_=false;
    });
  }
  if(addPanel_){
    addPanel_->setEnabled(count<3);
    addPanel_->setToolTip(count>=3?tr("한 줄에는 구조를 최대 3개까지 배치할 수 있습니다. 새 렌더링 줄을 추가하세요."):tr("현재 Figure 줄에 구조를 추가합니다."));
  }
  previous_->setEnabled(group_>0);const bool last=group_+1>=rows_.size();next_->setText(last?tr("Figure 적용"):tr("다음 줄"));
  status_->setText(tr("렌더링 줄 %1 / %2 · %3개 구조%4").arg(group_+1).arg(rows_.size()).arg(count).arg(failed?tr(" · 원자 대응이 다른 구조는 직접 회전하세요."):QString()));
  changing_=false;
}
QJsonObject FigureSetupDialog::settings() const {
  QJsonArray order,sources,rows;
  for(int index:order_)order.append(paths_[index]);
  for(const auto& path:paths_)sources.append(path);
  for(const auto& indices:rows_){QJsonArray row;for(int index:indices)row.append(paths_[index]);rows.append(row);}
  QJsonObject retained;for(auto it=retainedHydrogens_.cbegin();it!=retainedHydrogens_.cend();++it)retained.insert(it.key(),it.value());
  return {{"sequence_version",2},{"sources",sources},{"order",order},{"rows_version",1},{"rows",rows},{"views",views_},{"indices",indices_->isChecked()},
    {"hide_hydrogens",hideHydrogens_->isChecked()},{"retained_hydrogens",retained},{"render_style",renderStyle_}};
}
QVector<si::DocumentFigure> FigureSetupDialog::figures() const { return figures_; }
void FigureSetupDialog::finish(){
  figures_.clear();const int groups=rows_.size();
  for(int group=0;group<groups;++group){
    const int count=rows_[group].size();
    if(count<=0)continue;
    const int reference=group==0&&count==3?1:0;
    constexpr int tileWidth=1200;
    constexpr int labelMargin=30;
    constexpr int labelGap=24;
    constexpr int moleculeHeight=1000;
    std::vector<std::unique_ptr<QTextDocument>> labels;
    double labelHeight=0;
    si::DocumentFigure figure;
    figure.pageWidthFraction=count/3.0;
    for(int index:rows_[group]){
      const auto label=si::structureLabelMarkup(structures_[index].structureName);
      auto text=std::make_unique<QTextDocument>();
      text->setDocumentMargin(0);
      QFont font("Times New Roman");font.setPixelSize(66);
      text->setDefaultFont(font);
      QTextOption wrap;wrap.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
      text->setDefaultTextOption(wrap);
      text->setTextWidth(tileWidth-2*labelMargin);
      text->setHtml(QStringLiteral("<p style='margin:0;text-align:center;color:black'>%1</p>").arg(si::formattedTextHtml(label)));
      labelHeight=std::max(labelHeight,text->size().height());
      labels.push_back(std::move(text));
      figure.structureNames.append(label);
    }
    QImage image(QSize(tileWidth*count,moleculeHeight+2*labelGap+qCeil(labelHeight)),QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for(int i=0;i<count;++i){
      const int index=rows_[group][i];auto atoms=structures_[index].atoms;
      if(i!=reference)alignTo(atoms,structures_[rows_[group][reference]].atoms);
      MoleculeViewWidget view;view.setRenderStyle(renderStyle_);view.setMolecule(atoms,{});view.setAtomIndicesVisible(indices_->isChecked());
      view.setHydrogenVisibility(hideHydrogens_->isChecked(),retainedHydrogens_.value(paths_[index],0));
      const auto slotKey=QStringLiteral("row:%1:%2").arg(group).arg(i);
      view.setCameraState(customRows_&&views_.contains(slotKey)?views_.value(slotKey).toObject():views_.value(paths_[index]).toObject());
      painter.drawImage(i*tileWidth,0,view.renderMolecule(QSize(tileWidth,moleculeHeight)));
      painter.save();
      painter.translate(i*tileWidth+labelMargin,moleculeHeight+labelGap);
      labels[i]->drawContents(&painter);
      painter.restore();
    }
    painter.end();image.setDotsPerMeterX(23622);image.setDotsPerMeterY(23622);QBuffer buffer(&figure.png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
    figure.pixelWidth=image.width();figure.pixelHeight=image.height();figures_.append(figure);
  }
  accept();
}
