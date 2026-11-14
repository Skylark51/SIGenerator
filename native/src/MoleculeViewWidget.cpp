#include "MoleculeViewWidget.hpp"
#include "MolecularStyle.hpp"
#include "si/SIModel.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QLineF>
#include <QWheelEvent>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

double covalentRadius(int atomicNumber) {
  switch (atomicNumber) {
  case 1: return 0.31; case 6: return 0.76; case 7: return 0.71; case 8: return 0.66;
  case 9: return 0.57; case 14: return 1.11; case 15: return 1.07; case 16: return 1.05;
  case 17: return 1.02; case 26: return 1.32; case 27: return 1.26; case 28: return 1.24;
  case 29: return 1.32; case 30: return 1.22; case 35: return 1.20;
  default: return 0.85;
  }
}
}

MoleculeViewWidget::MoleculeViewWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setRenderStyle(currentYoungcraftStyle());
  setObjectName(QStringLiteral("moleculeView"));
  setMinimumSize(320, 260);
  setFocusPolicy(Qt::StrongFocus);
  setToolTip(tr("왼쪽 드래그: 회전, 오른쪽 드래그: 이동, 휠: 확대/축소 · 선택 구조 모드: 원자 2/3/4개로 측정, Esc로 선택 해제"));
}

void MoleculeViewWidget::setMolecule(const std::vector<si::Atom>& atoms, const std::vector<si::SpinDensity>& spins, const std::string& spinBasisLevel, bool spinFallback) {
  atoms_ = atoms; spins_ = spins; spinBasisLevel_=spinBasisLevel;spinFallback_=spinFallback;selectedAtoms_.clear(); rebuildBonds(); resetView(); update();
}
void MoleculeViewWidget::clearMolecule() { atoms_.clear(); spins_.clear();spinBasisLevel_="unknown";spinFallback_=false;bonds_.clear(); selectedAtoms_.clear(); update(); }
void MoleculeViewWidget::setInteractionMode(InteractionMode mode) { mode_ = mode; selectedAtoms_.clear(); emit geometrySelectionChanged(selectedAtoms_); update(); }
void MoleculeViewWidget::setAtomIndicesVisible(bool visible) { showIndices_ = visible; update(); }
void MoleculeViewWidget::setHydrogenVisibility(bool hideHydrogens, int retainedHydrogenIndex) {
  hideHydrogens_ = hideHydrogens;
  retainedHydrogenIndex_ = retainedHydrogenIndex;
  if(std::any_of(selectedAtoms_.begin(),selectedAtoms_.end(),[this](int id){return !atomVisible(id);}))clearSelection();
  update();
}
bool MoleculeViewWidget::atomVisible(int atomIndex) const {
  if(atomIndex<1 || static_cast<size_t>(atomIndex)>atoms_.size())return false;
  return !hideHydrogens_ || atoms_[static_cast<size_t>(atomIndex-1)].atomicNumber!=1 || atomIndex==retainedHydrogenIndex_;
}
void MoleculeViewWidget::resetView() { yaw_=-25.0; pitch_=20.0; roll_=0.0; zoom_=1.0; pan_={}; update(); emit cameraChanged(); }
void MoleculeViewWidget::clearSelection() { selectedAtoms_.clear(); emit geometrySelectionChanged(selectedAtoms_); update(); }
QString MoleculeViewWidget::selectedGeometryText() const {
  if(mode_!=InteractionMode::Geometry||selectedAtoms_.size()<2)return {};
  QStringList ids;for(int id:selectedAtoms_)ids.append(QString::number(id));
  std::optional<double> value;
  if(selectedAtoms_.size()==2)value=si::distance(atoms_,selectedAtoms_[0],selectedAtoms_[1]);
  else if(selectedAtoms_.size()==3)value=si::angle(atoms_,selectedAtoms_[0],selectedAtoms_[1],selectedAtoms_[2]);
  else if(selectedAtoms_.size()==4)value=si::dihedral(atoms_,selectedAtoms_[0],selectedAtoms_[1],selectedAtoms_[2],selectedAtoms_[3]);
  return ids.join("–")+": "+(value?QString::number(*value,'f',selectedAtoms_.size()==2?3:2):QStringLiteral("N/A"))+
    (selectedAtoms_.size()==2?QStringLiteral(" Å"):QStringLiteral("°"));
}
void MoleculeViewWidget::initializeGL() {}
void MoleculeViewWidget::resizeGL(int, int) {}

QVector<MoleculeViewWidget::ProjectedAtom> MoleculeViewWidget::projectedAtoms(QSize size) const {
  const int width=size.width(),height=size.height();
  QVector<ProjectedAtom> output; if (atoms_.empty()) return output;
  std::array<double,3> center{};size_t visibleCount=0;
  for(size_t index=0;index<atoms_.size();++index)if(atomVisible(static_cast<int>(index)+1)){
    for(int i=0;i<3;++i)center[i]+=atoms_[index].xyz[i];
    ++visibleCount;
  }
  if(visibleCount>0)for(double& v:center)v/=static_cast<double>(visibleCount);
  const double cy=std::cos(yaw_*std::numbers::pi/180.0), sy=std::sin(yaw_*std::numbers::pi/180.0), cp=std::cos(pitch_*std::numbers::pi/180.0), sp=std::sin(pitch_*std::numbers::pi/180.0);
  double extent=1.0;for(size_t index=0;index<atoms_.size();++index)if(atomVisible(static_cast<int>(index)+1)){
    const auto& atom=atoms_[index];
    for(int i=0;i<3;++i)extent=std::max(extent,std::abs(atom.xyz[i]-center[i])+atomRadius(atom));
  }
  const double cr=std::cos(roll_*std::numbers::pi/180.0),sr=std::sin(roll_*std::numbers::pi/180.0);
  const double scale=std::min(width,height)*0.38/extent*zoom_;
  output.reserve(static_cast<qsizetype>(atoms_.size()));
  for(size_t i=0;i<atoms_.size();++i){const auto& a=atoms_[i];const double x=a.xyz[0]-center[0],y=a.xyz[1]-center[1],z=a.xyz[2]-center[2];const double x1=cy*x+sy*z,z1=-sy*x+cy*z;const double y1=cp*y-sp*z1,z2=sp*y+cp*z1;const double perspective=renderStyle_.value("perspective").toBool()?1.0/(1.0+z2/(extent*8.0)):1.0;output.append({{width/2.0+pan_.x()*std::min(width,height)+(cr*x1-sr*y1)*scale*perspective,height/2.0+pan_.y()*std::min(width,height)-(sr*x1+cr*y1)*scale*perspective},z2,scale*perspective*atomRadius(a),static_cast<int>(i)+1});}
  return output;
}

void MoleculeViewWidget::setRenderStyle(const QJsonObject& style){renderStyle_=style;sphereCache_.clear();update();}
double MoleculeViewWidget::atomRadius(const si::Atom& atom) const {
  const bool metal=(atom.atomicNumber>=21&&atom.atomicNumber<=30)||(atom.atomicNumber>=39&&atom.atomicNumber<=80);
  double normalized=atom.atomicNumber==1?0.72:std::clamp(0.90+(covalentRadius(atom.atomicNumber)-0.65)*0.38,0.88,1.24);
  if(metal)normalized=std::min(1.30,normalized+0.08);
  const double override=renderStyle_.value("elementRadiusScales").toObject().value(QString::number(atom.atomicNumber)).toDouble(1);
  return renderStyle_.value("atomScale").toDouble(0.28)*normalized*override;
}

QRect MoleculeViewWidget::captureRect() const {
  if(!captureFraming_)return rect();
  QSize frame=QSize(1200,1000).scaled(size(),Qt::KeepAspectRatio);
  return QRect(QPoint((width()-frame.width())/2,(height()-frame.height())/2),frame);
}
QVector<MoleculeViewWidget::ProjectedAtom> MoleculeViewWidget::projectedAtoms() const {
  const auto frame=captureRect();auto atoms=projectedAtoms(frame.size());for(auto& atom:atoms)atom.point+=frame.topLeft();return atoms;
}
void MoleculeViewWidget::paintGL() {
  QPainter painter(this);
  const auto frame=captureRect();
  painter.fillRect(rect(),QColor(235,237,240));
  painter.save();painter.translate(frame.topLeft());paintScene(painter,frame.size(),false);painter.restore();
  if(captureFraming_){painter.setPen(QColor(194,198,205));painter.setBrush(Qt::NoBrush);painter.drawRect(frame.adjusted(0,0,-1,-1));}
}

QJsonObject MoleculeViewWidget::cameraState() const {
  return {{"yaw",yaw_},{"pitch",pitch_},{"roll",roll_},{"zoom",zoom_},{"pan_x",pan_.x()},{"pan_y",pan_.y()}};
}
void MoleculeViewWidget::setCameraState(const QJsonObject& state) {
  auto value=[&](const char* key,double fallback){const double n=state.value(key).toDouble(fallback);return std::isfinite(n)?n:fallback;};
  yaw_=value("yaw",-25);pitch_=value("pitch",20);roll_=value("roll",0);
  zoom_=std::clamp(value("zoom",1),0.15,8.0);pan_={value("pan_x",0),value("pan_y",0)};update();emit cameraChanged();
}
QImage MoleculeViewWidget::renderMolecule(QSize size) const {
  QImage image(size*2,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
  QPainter painter(&image);paintScene(painter,image.size(),true);painter.end();
  return image.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
void MoleculeViewWidget::paintScene(QPainter& painter,QSize size,bool clean) const {
  const int width=size.width(),height=size.height();
  painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
  if(!clean)painter.fillRect(QRect(QPoint(),size),QColor(renderStyle_.value("background").toString("#ffffffff")));
  if(atoms_.empty()){if(!clean){painter.setPen(QColor(105,115,130));painter.drawText(QRect(QPoint(),size),Qt::AlignCenter,tr("Gaussian 파일을 선택하면 분자가 표시됩니다."));}return;}
  const auto uiFont=painter.font();
  const auto projected=projectedAtoms(size);
  const double unit=std::min(width,height)/500.0;
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  // Paint cylinders and spheres together in camera depth order.
  struct Primitive { double depth; int atom; int bond; };
  QVector<Primitive> primitives;
  for(const auto& item:projected)if(atomVisible(item.index))primitives.append({item.depth,item.index-1,-1});
  for(int i=0;i<bonds_.size();++i){
    const auto& b=bonds_[i];
    if(!atomVisible(b.first+1)||!atomVisible(b.second+1))continue;
    primitives.append({(projected[b.first].depth+projected[b.second].depth)/2,-1,i});
  }
  std::sort(primitives.begin(),primitives.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});
  for(const auto& primitive:primitives){
    if(primitive.bond>=0){const auto& b=bonds_[primitive.bond];QPointF a=projected[b.first].point,c=projected[b.second].point;const auto axis=c-a;const double length=QLineF(a,c).length();
      const double insetA=projected[b.first].radius*0.92,insetC=projected[b.second].radius*0.92;
      if(length<=insetA+insetC)continue;
      a+=axis*(insetA/length);c-=axis*(insetC/length);const auto m=(a+c)/2;
      auto first=molecularElementColor(atoms_[b.first].atomicNumber,renderStyle_),second=molecularElementColor(atoms_[b.second].atomicNumber,renderStyle_);
      const int mode=renderStyle_.value("bondColorMode").toInt();
      if(mode==1)first=second=QColor(renderStyle_.value("bondColor").toString("#ffa0a6b2"));
      else if(mode==2)first=second=QColor((first.red()+second.red())/2,(first.green()+second.green())/2,(first.blue()+second.blue())/2);
      const double scale=projected[b.first].radius/std::max(0.001,atomRadius(atoms_[b.first]));
      const double thickness=2*renderStyle_.value("bondRadius").toDouble(0.032)*scale;
      auto cylinder=[&](QPointF from,QPointF to,const QColor& color){const QLineF axis(from,to);if(axis.length()<0.01)return;const auto normal=QPointF(-(to.y()-from.y()),to.x()-from.x())/axis.length()*thickness/2;
        QLinearGradient shade(from-normal,from+normal);shade.setColorAt(0,color.darker(145));shade.setColorAt(0.4,color.lighter(108));shade.setColorAt(1,color.darker(145));
        painter.setOpacity(renderStyle_.value("bondOpacity").toDouble(1));painter.setPen(QPen(QBrush(shade),thickness,Qt::SolidLine,Qt::RoundCap));painter.drawLine(from,to);painter.setOpacity(1);};
      cylinder(a,m,first);cylinder(m,c,second);continue;}
    const auto& item=projected[primitive.atom];const auto& atom=atoms_[primitive.atom];
    const bool selected=!clean&&selectedAtoms_.contains(item.index);
    const QRectF circle(item.point-QPointF(item.radius,item.radius),QSizeF(item.radius*2,item.radius*2));
    const auto color=molecularElementColor(atom.atomicNumber,renderStyle_);
    if(!sphereCache_.contains(atom.atomicNumber))sphereCache_.insert(atom.atomicNumber,molecularSphere(color,renderStyle_));
    painter.drawImage(circle,sphereCache_.value(atom.atomicNumber));
    if(selected){painter.setPen(QPen(QColor(255,174,0),3*unit));painter.setBrush(Qt::NoBrush);painter.drawEllipse(circle);}

    if(showIndices_){painter.setPen(color.lightnessF()>0.5?Qt::black:Qt::white);QFont font=painter.font();font.setPixelSize(std::max(8,int(item.radius*0.95)));font.setBold(true);painter.setFont(font);painter.drawText(circle,Qt::AlignCenter,QString::number(item.index));}
    if(selected&&mode_==InteractionMode::Geometry){painter.setPen(Qt::black);painter.drawText(circle.translated(item.radius,-item.radius),Qt::AlignCenter,QString::number(selectedAtoms_.indexOf(item.index)+1));}
  }
  if(clean)return;
  painter.setFont(uiFont);
  if(mode_==InteractionMode::Geometry&&!selectedAtoms_.isEmpty()){
    painter.setPen(QPen(QColor(215,135,0),2*unit,Qt::DashLine));
    for(qsizetype i=1;i<selectedAtoms_.size();++i)
      painter.drawLine(projected[selectedAtoms_[i-1]-1].point,projected[selectedAtoms_[i]-1].point);
    const auto text=selectedGeometryText();
    if(!text.isEmpty()){
      const QRectF box(10,height-42,std::min(width-20.0,painter.fontMetrics().horizontalAdvance(text)+24.0),32);
      painter.setPen(QPen(QColor(185,145,60),1));painter.setBrush(QColor(255,249,230,245));painter.drawRoundedRect(box,6,6);
      painter.setPen(QColor(65,48,20));painter.drawText(box,Qt::AlignCenter,text);
    }
  }
  if(mode_==InteractionMode::SpinDensity&&selectedAtoms_.size()==1){const int id=selectedAtoms_.front();const auto found=std::find_if(projected.begin(),projected.end(),[id](const auto& item){return item.index==id;});if(found!=projected.end()){double spin=0.0;bool hasSpin=false;for(const auto& value:spins_)if(value.atomIndex==id){spin=value.spin;hasSpin=true;break;}const auto& atom=atoms_[static_cast<size_t>(id-1)];const QString basis=QString::fromStdString(spinBasisLevel_)+(spinFallback_?QStringLiteral(" fallback"):QString());const QString text=QStringLiteral("%1(%2)\nSpin: %3\nBasis: %4").arg(QString::fromStdString(atom.symbol)).arg(id).arg(hasSpin?QString::number(spin,'f',3):QStringLiteral("N/A"),basis);QRectF box(found->point+QPointF(18,-62),QSizeF(150,58));if(box.right()>width-8)box.moveRight(width-8);if(box.top()<34)box.moveTop(34);painter.setPen(QPen(QColor(108,126,150),1.2));painter.setBrush(QColor(238,245,255,245));painter.drawRoundedRect(box,8,8);painter.setPen(QColor(42,55,72));painter.drawText(box.adjusted(9,5,-6,-4),Qt::AlignLeft|Qt::AlignVCenter,text);}}
  painter.setPen(QColor(75,85,100));painter.drawText(QRect(10,8,width-20,24),Qt::AlignLeft|Qt::AlignVCenter,mode_==InteractionMode::SpinDensity?tr("스핀 밀도 모드 (Spin Density Mode)"):mode_==InteractionMode::Geometry?tr("선택 구조 모드 (Selected Geometry Mode)"):tr("분자 보기 (Molecular Viewer)"));
}

void MoleculeViewWidget::rebuildBonds(){bonds_.clear();for(size_t i=0;i<atoms_.size();++i)for(size_t j=i+1;j<atoms_.size();++j){const auto&a=atoms_[i],&b=atoms_[j];const double dx=a.xyz[0]-b.xyz[0],dy=a.xyz[1]-b.xyz[1],dz=a.xyz[2]-b.xyz[2],distance=std::sqrt(dx*dx+dy*dy+dz*dz);const double cutoff=1.22*(covalentRadius(a.atomicNumber)+covalentRadius(b.atomicNumber));if(distance>0.2&&distance<=cutoff)bonds_.append({static_cast<int>(i),static_cast<int>(j)});}}
int MoleculeViewWidget::pickAtom(const QPointF& position) const {int picked=-1;double best=1e9,bestDepth=1e9;for(const auto& item:projectedAtoms()){if(!atomVisible(item.index))continue;const double d=QLineF(position,item.point).length();if(d<=item.radius+7.0&&(item.depth<bestDepth||(item.depth==bestDepth&&d<best))){picked=item.index;best=d;bestDepth=item.depth;}}return picked;}
void MoleculeViewWidget::handleClick(const QPointF& position){const int picked=pickAtom(position);if(picked<1)return;const auto& atom=atoms_[static_cast<size_t>(picked-1)];if(mode_==InteractionMode::Geometry){if(selectedAtoms_.contains(picked))selectedAtoms_.removeAll(picked);else{if(selectedAtoms_.size()>=4)selectedAtoms_.clear();selectedAtoms_.append(picked);}emit geometrySelectionChanged(selectedAtoms_);}else{selectedAtoms_={picked};double spin=0.0;bool found=false;for(const auto& value:spins_)if(value.atomIndex==picked){spin=value.spin;found=true;break;}emit atomPicked(picked,QString::fromStdString(atom.symbol),spin,found);}update();}
void MoleculeViewWidget::mousePressEvent(QMouseEvent* event){setFocus();lastMouse_=event->pos();dragging_=true;moved_=false;dragButton_=event->button();event->accept();}
void MoleculeViewWidget::mouseMoveEvent(QMouseEvent* event){if(!dragging_)return;const QPoint delta=event->pos()-lastMouse_;if(delta.manhattanLength()>2)moved_=true;if(dragButton_==Qt::RightButton||event->modifiers().testFlag(Qt::ShiftModifier))pan_+=QPointF(delta)/std::max(1,std::min(captureRect().width(),captureRect().height()));else if(event->modifiers().testFlag(Qt::AltModifier)){roll_+=delta.x()*0.6;}else{yaw_+=delta.x()*0.6;pitch_+=delta.y()*0.6;}lastMouse_=event->pos();update();emit cameraChanged();}
void MoleculeViewWidget::mouseReleaseEvent(QMouseEvent* event){if(dragging_&&!moved_&&event->button()==Qt::LeftButton)handleClick(event->position());dragging_=false;dragButton_=Qt::NoButton;event->accept();}
void MoleculeViewWidget::wheelEvent(QWheelEvent* event){zoom_=std::clamp(zoom_*std::pow(1.0015,event->angleDelta().y()),0.15,8.0);update();emit cameraChanged();event->accept();}
void MoleculeViewWidget::keyPressEvent(QKeyEvent* event){if(event->key()==Qt::Key_Escape){clearSelection();event->accept();return;}QOpenGLWidget::keyPressEvent(event);}
