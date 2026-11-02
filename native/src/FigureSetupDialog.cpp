#include "FigureSetupDialog.hpp"
#include "MoleculeViewWidget.hpp"
#include "MolecularStyle.hpp"
#include "si/StructureLabels.hpp"
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QFontMetrics>
#include <QPushButton>
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
    QQuaternion rotation(float(v[0][best]),float(v[1][best]),float(v[2][best]),float(v[3][best]));
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
  setObjectName("figureSetupDialog");setWindowTitle(tr("SI 분자 Figure 준비"));resize(1250,650);
  QSet<int> used;QJsonArray sourceSequence;for(const auto& path:paths_)sourceSequence.append(path);
  // Version 1 saved an order produced by a filename substring heuristic.
  // Restore deliberate panel edits only for this exact, unchanged input list.
  if(settings.value("sequence_version").toInt()==2&&settings.value("sources").toArray()==sourceSequence)
    for(const auto& value:settings.value("order").toArray()){
      const int index=paths_.indexOf(value.toString());if(index>=0&&!used.contains(index)){order_.append(index);used.insert(index);}
    }
  for(int i=0;i<structures_.size();++i)if(!used.contains(i))order_.append(i);
  auto* layout=new QVBoxLayout(this);
  auto* instruction=new QLabel(tr("목록 위→아래 순서 그대로: 첫 RC · TS · PC (3개), 이후 TS · PC (2개).\n구조 선택으로 위치 교환 · 왼쪽 드래그 회전 · Alt+드래그 화면 내 회전 · 오른쪽 드래그 이동 · 휠 확대. 좌우는 직접 지정하세요."),this);
  instruction->setWordWrap(true);layout->addWidget(instruction);
  auto* paletteLabel=new QLabel(tr("Youngcraft 저장 스타일: %1").arg(renderStyle_.value("name").toString()),this);paletteLabel->setObjectName("youngcraftStyleName");layout->addWidget(paletteLabel);
  auto* controls=new QHBoxLayout;
  linked_=new QCheckBox(tr("카메라 함께 회전"),this);linked_->setObjectName("linkFigureCameras");linked_->setChecked(true);controls->addWidget(linked_);
  indices_=new QCheckBox(tr("원자 번호 표시"),this);indices_->setChecked(settings.value("indices").toBool(false));controls->addWidget(indices_);
  auto* match=new QPushButton(tr("TS 기준 정렬 및 뷰 맞추기"),this);match->setObjectName("alignFigureViews");controls->addWidget(match);
  auto* reset=new QPushButton(tr("뷰 초기화"),this);controls->addWidget(reset);layout->addLayout(controls);
  panelWidget_=new QWidget(this);panels_=new QVBoxLayout(panelWidget_);panels_->setContentsMargins(0,0,0,0);layout->addWidget(panelWidget_,1);
  status_=new QLabel(this);status_->setWordWrap(true);layout->addWidget(status_);
  auto* buttons=new QDialogButtonBox(this);
  previous_=buttons->addButton(tr("이전"),QDialogButtonBox::ActionRole);previous_->setObjectName("previousFigureGroup");
  next_=buttons->addButton(tr("다음"),QDialogButtonBox::ActionRole);next_->setObjectName("nextFigureGroup");
  auto* cancel=buttons->addButton(QDialogButtonBox::Cancel);layout->addWidget(buttons);
  connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
  connect(previous_,&QPushButton::clicked,this,[this]{storeGroup();--group_;showGroup();});
  connect(next_,&QPushButton::clicked,this,[this]{storeGroup();const int start=group_==0?0:3+(group_-1)*2;const int count=group_==0?3:2;if(start+count<order_.size()){++group_;showGroup();}else finish();});
  connect(indices_,&QCheckBox::toggled,this,[this](bool checked){for(auto* view:viewers_)view->setAtomIndicesVisible(checked);});
  connect(reset,&QPushButton::clicked,this,[this]{changing_=true;for(auto* view:viewers_)view->resetView();changing_=false;});
  connect(match,&QPushButton::clicked,this,[this]{
    const int start=group_==0?0:3+(group_-1)*2;
    const int reference=group_==0&&viewers_.size()==3?1:0;
    const auto camera=viewers_[reference]->cameraState();const auto& target=structures_[order_[start+reference]].atoms;
    int failed=0;changing_=true;
    for(int i=0;i<viewers_.size();++i){auto atoms=structures_[order_[start+i]].atoms;if(i!=reference&&!alignTo(atoms,target))++failed;viewers_[i]->setMolecule(atoms,{});viewers_[i]->setCameraState(camera);}
    changing_=false;
    status_->setText(failed?tr("원자 수 또는 원소 순서가 다른 구조 %1개는 자동 정렬할 수 없습니다. 카메라 함께 회전을 끄고 직접 뷰를 조정하세요.").arg(failed):tr("TS에 강체 회전 정렬했습니다. 원본 좌표는 보존됩니다."));
  });
  showGroup();
}
void FigureSetupDialog::storeGroup(){
  const int start=group_==0?0:3+(group_-1)*2;
  for(int i=0;i<viewers_.size();++i)views_.insert(paths_[order_[start+i]],viewers_[i]->cameraState());
}
void FigureSetupDialog::showGroup(){
  changing_=true;viewers_.clear();selectors_.clear();
  while(auto* item=panels_->takeAt(0)){if(auto* widget=item->widget()){widget->hide();widget->deleteLater();}delete item;}
  auto* rowWidget=new QWidget(panelWidget_);auto* row=new QHBoxLayout(rowWidget);row->setContentsMargins(0,0,0,0);panels_->addWidget(rowWidget);
  const int start=group_==0?0:3+(group_-1)*2;
  const int count=std::min(group_==0?3:2,int(order_.size())-start);
  const int reference=group_==0&&count==3?1:0;
  int failed=0;
  for(int i=0;i<count;++i){
    auto* column=new QWidget(rowWidget);auto* vertical=new QVBoxLayout(column);row->addWidget(column,1);
    vertical->addWidget(new QLabel(group_==0&&count==3?(i==0?tr("왼쪽 IRC"):i==1?tr("TS"):tr("오른쪽 IRC")):(i==0?tr("TS / 구조 1"):tr("IRC / 구조 2")),column));
    auto* selector=new QComboBox(column);selector->setObjectName(QString("figureStructure%1").arg(i));
    // Swaps are available across the entire sequence; no structure is duplicated.
    for(int index:order_)selector->addItem(structures_[index].structureName,index);
    selector->setCurrentIndex(start+i);vertical->addWidget(selector);selectors_.append(selector);
    auto* viewer=new MoleculeViewWidget(column);viewer->setObjectName(QString("figureView%1").arg(i));viewer->setCaptureFraming(true);viewer->setMinimumSize(240,280);vertical->addWidget(viewer,1);
    const int index=order_[start+i];auto atoms=structures_[index].atoms;
    if(i!=reference&&!alignTo(atoms,structures_[order_[start+reference]].atoms))++failed;
    viewer->setRenderStyle(renderStyle_);viewer->setMolecule(atoms,{});viewer->setAtomIndicesVisible(indices_->isChecked());
    if(views_.contains(paths_[index]))viewer->setCameraState(views_.value(paths_[index]).toObject());
    viewers_.append(viewer);
    connect(selector,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,i,start,selector]{
      if(changing_)return;storeGroup();const int other=order_.indexOf(selector->currentData().toInt());std::swap(order_[start+i],order_[other]);showGroup();
    });
    connect(viewer,&MoleculeViewWidget::cameraChanged,this,[this,viewer]{
      if(changing_||!linked_->isChecked())return;changing_=true;for(auto* other:viewers_)if(other!=viewer)other->setCameraState(viewer->cameraState());changing_=false;
    });
  }
  previous_->setEnabled(group_>0);const bool last=start+count>=order_.size();next_->setText(last?tr("Figure 적용"):tr("다음 2개"));
  status_->setText(tr("단계 %1 / %2 · %3개 구조%4").arg(group_+1).arg(order_.size()<=3?1:1+(order_.size()-3+1)/2).arg(count).arg(failed?tr(" · 원자 대응이 다른 구조는 직접 회전하세요."):QString()));
  changing_=false;
}
QJsonObject FigureSetupDialog::settings() const {
  QJsonArray order,sources;for(int index:order_)order.append(paths_[index]);for(const auto& path:paths_)sources.append(path);return {{"sequence_version",2},{"sources",sources},{"order",order},{"views",views_},{"indices",indices_->isChecked()},{"render_style",renderStyle_}};
}
QVector<si::DocumentFigure> FigureSetupDialog::figures() const { return figures_; }
void FigureSetupDialog::finish(){
  figures_.clear();const int groups=order_.size()<=3?1:1+(order_.size()-3+1)/2;
  for(int group=0;group<groups;++group){
    const int start=group==0?0:3+(group-1)*2,count=std::min(group==0?3:2,int(order_.size())-start);
    if(count<=0)continue;
    const int reference=group==0&&count==3?1:0;
    QImage image(QSize(1200*count,1100),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);
    si::DocumentFigure figure;
    for(int i=0;i<count;++i){
      const int index=order_[start+i];auto atoms=structures_[index].atoms;
      if(i!=reference)alignTo(atoms,structures_[order_[start+reference]].atoms);
      MoleculeViewWidget view;view.setRenderStyle(renderStyle_);view.setMolecule(atoms,{});view.setAtomIndicesVisible(indices_->isChecked());view.setCameraState(views_.value(paths_[index]).toObject());
      painter.drawImage(i*1200,0,view.renderMolecule());
      const auto label=si::structureLabelMarkup(structures_[index].structureName);
      QTextDocument text;text.setDocumentMargin(0);text.setTextWidth(1160);
      int pixels=42;
      do {QFont font("Times New Roman");font.setPixelSize(pixels);text.setDefaultFont(font);
        text.setHtml(QStringLiteral("<p style='margin:0;text-align:center;color:black'>%1</p>").arg(si::formattedTextHtml(label)));
        if(text.size().height()<=90||pixels<=18)break;pixels-=2;
      }while(true);
      painter.save();painter.translate(i*1200+20,1005+(90-text.size().height())/2);text.drawContents(&painter,QRectF(0,0,1160,90));painter.restore();
      figure.structureNames.append(label);
    }
    painter.end();image.setDotsPerMeterX(23622);image.setDotsPerMeterY(23622);QBuffer buffer(&figure.png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
    figure.pixelWidth=image.width();figure.pixelHeight=image.height();figures_.append(figure);
  }
  accept();
}
