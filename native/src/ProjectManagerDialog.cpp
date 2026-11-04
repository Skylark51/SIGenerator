#include "ProjectManagerDialog.hpp"
#include "ProjectStore.hpp"
#include "WindowGeometry.hpp"
#include "AppTheme.hpp"
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QToolButton>
#include <QPainter>
#include <algorithm>

namespace {
QSettings settings(){return QSettings("SI Generator",qEnvironmentVariableIsSet("SI_GENERATOR_TEST_SETTINGS")?"SIGeneratorTests":"SIGenerator");}
QString projectName(const QString& path){return QFileInfo(path).completeBaseName().remove(".si_project");}
constexpr int projectsPerPage=3;
struct Theme {const char* name;const char* accent;const char* soft;const char* background;};
constexpr Theme themes[]={{"오션 블루","#2563a6","#e8f3ff","#f1f7fd"},{"세이지 그린","#287466","#e6f4ee","#f0f8f3"},{"라벤더","#7552a8","#f1eafa","#f8f4fc"}};
}

ProjectManagerDialog::ProjectManagerDialog(QWidget* parent):QDialog(parent){
  setObjectName("projectManagerDialog");setWindowTitle(tr("SI Generator — 프로젝트 관리"));resize(940,830);
  setWindowFlag(Qt::WindowMaximizeButtonHint);setSizeGripEnabled(true);
  auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
  auto* scroll=new QScrollArea(this);scroll->setObjectName("projectManagerScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
  auto* content=new QWidget(scroll);auto* layout=new QVBoxLayout(content);layout->setContentsMargins(28,24,28,24);layout->setSpacing(12);
  layout->setSizeConstraint(QLayout::SetMinAndMaxSize);scroll->setWidget(content);outer->addWidget(scroll);setMinimumSize(360,240);
  auto* header=new QHBoxLayout;auto* brand=new QLabel("SI GENERATOR",this);brand->setObjectName("managerBrand");header->addWidget(brand);header->addStretch();
  auto* themeGroup=new QButtonGroup(this);
  for(int i=0;i<3;++i){auto* button=new QToolButton(this);button->setObjectName(QString("projectTheme%1").arg(i));button->setCheckable(true);button->setFixedSize(30,30);button->setToolTip(tr(themes[i].name));button->setAccessibleName(tr("색상 테마: %1").arg(tr(themes[i].name)));QPixmap swatch(20,20);swatch.fill(Qt::transparent);QPainter painter(&swatch);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(QColor(themes[i].accent));painter.drawEllipse(2,2,16,16);painter.end();button->setIcon(QIcon(swatch));themeGroup->addButton(button,i);header->addWidget(button);connect(button,&QToolButton::clicked,this,[this,i]{applyTheme(i);});}
  layout->addLayout(header);
  auto* title=new QLabel(tr("Supporting Information을 위한 프로젝트를 생성 및 관리하세요."),this);title->setObjectName("managerTitle");title->setWordWrap(true);layout->addWidget(title);
  auto* note=new QLabel(tr("작업할 SI를 선택하거나 새 프로젝트를 만드세요.\n\n프로젝트 안에서 하위 반응 단계와 단계별 표를 구성할 수 있습니다."),this);note->setObjectName("managerIntro");note->setWordWrap(true);layout->addWidget(note);
  auto* recentHeading=new QLabel(tr("최근 프로젝트"),this);recentHeading->setObjectName("recentProjectsHeading");layout->addWidget(recentHeading);
  projects_=new QListWidget(this);projects_->setObjectName("recentProjectsList");projects_->setSpacing(5);projects_->setWordWrap(true);
  projects_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);projects_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);projects_->setFixedHeight(308);layout->addWidget(projects_);
  auto* paging=new QHBoxLayout;previous_=new QPushButton(tr("‹ 이전"),this);previous_->setObjectName("previousProjectPage");next_=new QPushButton(tr("다음 ›"),this);next_->setObjectName("nextProjectPage");pageLabel_=new QLabel(this);pageLabel_->setObjectName("projectPageLabel");paging->addStretch();paging->addWidget(previous_);paging->addWidget(pageLabel_);paging->addWidget(next_);layout->addLayout(paging);
  connect(previous_,&QPushButton::clicked,this,[this]{--page_;renderProjects();});connect(next_,&QPushButton::clicked,this,[this]{++page_;renderProjects();});
  auto* actions=new QHBoxLayout;open_=new QPushButton(tr("선택한 프로젝트 열기"),this);open_->setObjectName("openSelectedProject");auto* browse=new QPushButton(tr("기존 프로젝트가 있으신가요?"),this);browse->setObjectName("browseProjectButton");actions->addWidget(open_);actions->addWidget(browse);actions->addStretch();layout->addLayout(actions);
  connect(open_,&QPushButton::clicked,this,&ProjectManagerDialog::openSelected);connect(browse,&QPushButton::clicked,this,&ProjectManagerDialog::chooseExisting);
  connect(projects_,&QListWidget::itemDoubleClicked,this,[this]{openSelected();});
  connect(projects_,&QListWidget::currentItemChanged,this,[this]{auto* item=projects_->currentItem();open_->setEnabled(item&&item->data(Qt::UserRole+1).toBool());});
  auto* newHeading=new QLabel(tr("새 프로젝트 만들기"),this);newHeading->setObjectName("newProjectHeading");layout->addWidget(newHeading);auto* form=new QFormLayout;form->setVerticalSpacing(10);layout->addLayout(form);
  name_=new QLineEdit(this);name_->setObjectName("newProjectName");name_->setPlaceholderText(tr("예: Deformylation SI"));form->addRow(tr("프로젝트 이름"),name_);
  auto* directory=new QHBoxLayout;location_=new QLineEdit(this);location_->setObjectName("newProjectLocation");location_->setText(settings().value("projectLibraryDirectory",QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/SI Generator Projects").toString());auto* folder=new QPushButton(tr("폴더 선택…"),this);directory->addWidget(location_,1);directory->addWidget(folder);form->addRow(tr("저장 위치"),directory);
  connect(folder,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,tr("프로젝트를 보관할 폴더"),location_->text());if(!path.isEmpty())location_->setText(path);});
  auto* bottom=new QHBoxLayout;auto* create=new QPushButton(tr("선택한 프로젝트 관리 시작!"),this);create->setObjectName("createProjectButton");create->setDefault(true);bottom->addWidget(create);bottom->addStretch();auto* close=new QPushButton(tr("오늘은 여기까지..."),this);bottom->addWidget(close);layout->addLayout(bottom);
  connect(create,&QPushButton::clicked,this,&ProjectManagerDialog::createProject);connect(close,&QPushButton::clicked,this,&QDialog::reject);
  const int theme=std::clamp(settings().value("projectManagerTheme",0).toInt(),0,2);themeGroup->button(theme)->setChecked(true);applyTheme(theme);refreshProjects();
}

void ProjectManagerDialog::showEvent(QShowEvent* event){
  QDialog::showEvent(event);
  QTimer::singleShot(0,this,[this]{fitWindowToScreen(this);});
}

void ProjectManagerDialog::applyTheme(int index){
  const auto& theme=themes[std::clamp(index,0,2)];settings().setValue("projectManagerTheme",index);
  applySiApplicationTheme(index);
  setStyleSheet(QString(R"(
    QDialog#projectManagerDialog, QScrollArea#projectManagerScroll, QScrollArea#projectManagerScroll > QWidget > QWidget {background:%3;}
    QLabel {color:#34465b;font-size:10pt;}
    QLabel#managerBrand {color:%1;font-size:10pt;font-weight:700;letter-spacing:2px;}
    QLabel#managerTitle {color:#24374c;font-size:19pt;font-weight:700;}
    QLabel#managerIntro {color:#63768b;font-size:10pt;}
    QLabel#recentProjectsHeading {color:%1;font-size:13pt;font-weight:700;border-left:4px solid %1;padding:3px 10px;}
    QLabel#newProjectHeading {color:%1;font-size:14pt;font-weight:600;padding:5px 0;}
    QListWidget {background:transparent;border:0;outline:0;}
    QListWidget::item {background:white;color:transparent;border:1px solid #d9e2ec;border-radius:10px;}
    QListWidget::item:selected {background:%2;border:2px solid %1;}
    QLabel#projectName {color:%1;font-size:13pt;font-weight:700;}
    QLabel#projectDetails {color:#51667c;font-size:9pt;}
    QLabel#projectPath {color:#7b8999;font-size:8pt;}
    QLabel#projectName[unavailable="true"] {color:#a05b65;}
    QPushButton,QToolButton {background:white;color:%1;border:1px solid #cbd9e5;border-radius:8px;padding:7px 12px;}
    QPushButton:hover,QToolButton:hover {background:%2;border-color:%1;}
    QPushButton:focus,QToolButton:focus {border:2px solid %1;}
    QPushButton:disabled {color:#9aa7b5;background:#edf1f5;border-color:#dde3ea;}
    QToolButton {padding:2px;}
    QToolButton:checked {background:%2;border:2px solid %1;}
    QToolButton#favoriteProject:checked {color:#c23e71;background:#fff0f5;border-color:#e6a4bf;}
    QPushButton#createProjectButton {background:%1;color:white;font-size:11pt;font-weight:700;padding:10px 18px;}
    QPushButton#createProjectButton:hover {background:%1;border:2px solid #24374c;}
    QLineEdit {background:white;color:#34465b;border:1px solid #cbd9e5;border-radius:7px;padding:7px;}
    QLineEdit:focus {border:1px solid %1;}
  )").arg(theme.accent,theme.soft,theme.background));
}

void ProjectManagerDialog::rememberProject(const QString& path){
  const auto absolute=QFileInfo(path).absoluteFilePath();auto prefs=settings();auto paths=prefs.value("recentProjects").toStringList();
  for(int i=paths.size()-1;i>=0;--i)if(paths[i].compare(absolute,Qt::CaseInsensitive)==0)paths.removeAt(i);
  paths.prepend(absolute);prefs.setValue("recentProjects",paths);
}

void ProjectManagerDialog::refreshProjects(){
  page_=0;selectedPath_.clear();renderProjects();
}

void ProjectManagerDialog::renderProjects(const QString& preferredPath){
  projects_->clear();auto prefs=settings();auto paths=prefs.value("recentProjects").toStringList();const auto pinned=prefs.value("pinnedProjects").toStringList();const auto favorites=prefs.value("favoriteProjects").toStringList();
  std::stable_sort(paths.begin(),paths.end(),[&](const QString& a,const QString& b){return pinned.contains(a,Qt::CaseInsensitive)&&!pinned.contains(b,Qt::CaseInsensitive);});
  if(!preferredPath.isEmpty()){const auto index=paths.indexOf(preferredPath);if(index>=0)page_=index/projectsPerPage;}
  const int pages=std::max(1,(int(paths.size())+projectsPerPage-1)/projectsPerPage);page_=std::clamp(page_,0,pages-1);
  previous_->setEnabled(page_>0);next_->setEnabled(page_+1<pages);pageLabel_->setText(tr("%1 / %2 · 총 %3개").arg(page_+1).arg(pages).arg(paths.size()));
  for(int row=page_*projectsPerPage;row<std::min(int(paths.size()),(page_+1)*projectsPerPage);++row){const auto path=paths[row];const QFileInfo info(path);ProjectState state;QString error;const bool available=info.isFile()&&ProjectStore::load(path,&state,&error);
    const auto title=state.projectName.isEmpty()?projectName(path):state.projectName;
    const auto detail=available?tr("구조 %1개 · 반응 단계 %2개 · 수정 %3").arg(state.files.size()).arg(state.reactionStages.size()).arg(info.lastModified().toString("yyyy-MM-dd HH:mm")):tr("프로젝트 파일을 찾거나 읽을 수 없습니다");
    auto* item=new QListWidgetItem(title,projects_);item->setData(Qt::UserRole,path);item->setData(Qt::UserRole+1,available);item->setSizeHint(QSize(0,94));
    auto* card=new QWidget(projects_);auto* rowLayout=new QHBoxLayout(card);rowLayout->setContentsMargins(16,10,12,10);auto* textLayout=new QVBoxLayout;textLayout->setSpacing(3);
    auto* name=new QLabel(title,card);name->setTextFormat(Qt::PlainText);name->setObjectName("projectName");name->setProperty("unavailable",!available);textLayout->addWidget(name);
    auto* details=new QLabel(detail,card);details->setObjectName("projectDetails");textLayout->addWidget(details);
    auto* file=new QLabel(card);file->setObjectName("projectPath");file->setTextFormat(Qt::PlainText);file->setText(QDir::toNativeSeparators(path));file->setMinimumWidth(0);file->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);file->setToolTip(path);textLayout->addWidget(file);rowLayout->addLayout(textLayout,1);
    for(int kind=0;kind<2;++kind){const bool pin=kind==0;auto* button=new QToolButton(card);button->setObjectName(pin?"pinProject":"favoriteProject");button->setText(pin?QString::fromUtf8("⚑"):QString::fromUtf8("♡"));button->setFixedSize(34,34);button->setCheckable(true);button->setChecked((pin?pinned:favorites).contains(path,Qt::CaseInsensitive));button->setAccessibleName(pin?tr("프로젝트 고정하기"):tr("좋아하는 프로젝트"));button->setToolTip(pin?(button->isChecked()?tr("고정 해제"):tr("고정하기 · 목록 맨 위에 표시")):(button->isChecked()?tr("하트 해제"):tr("하트 · 좋아하는 작업 표시")));if(!pin&&button->isChecked())button->setText(QString::fromUtf8("♥"));rowLayout->addWidget(button);
      connect(button,&QToolButton::clicked,this,[this,path,pin](bool checked){auto prefs=settings();const auto key=pin?"pinnedProjects":"favoriteProjects";auto values=prefs.value(key).toStringList();for(int i=values.size()-1;i>=0;--i)if(values[i].compare(path,Qt::CaseInsensitive)==0)values.removeAt(i);if(checked)values.append(path);prefs.setValue(key,values);if(pin){page_=0;renderProjects();}else renderProjects(path);});
    }
    // Let the list receive selection and double-click events on all text areas.
    for(auto* label:card->findChildren<QLabel*>())label->setAttribute(Qt::WA_TransparentForMouseEvents);
    projects_->setItemWidget(item,card);item->setToolTip(available?path:error);
  }
  if(paths.isEmpty()){auto* empty=new QListWidgetItem(tr("최근 프로젝트가 없습니다. 새 프로젝트를 만들거나 기존 프로젝트를 찾아 여세요."),projects_);empty->setFlags(Qt::NoItemFlags);}
  open_->setEnabled(false);if(!paths.isEmpty()){int current=0;for(int i=0;i<projects_->count();++i)if(projects_->item(i)->data(Qt::UserRole).toString()==preferredPath)current=i;projects_->setCurrentRow(current);}
}

bool ProjectManagerDialog::selectProject(const QString& path){
  ProjectState state;QString error;if(!ProjectStore::load(path,&state,&error)){QMessageBox::warning(this,tr("프로젝트 열기 실패"),error);return false;}
  selectedPath_=QFileInfo(path).absoluteFilePath();rememberProject(selectedPath_);accept();return true;
}
void ProjectManagerDialog::openSelected(){if(auto* item=projects_->currentItem()){const auto path=item->data(Qt::UserRole).toString();if(!path.isEmpty())selectProject(path);}}
void ProjectManagerDialog::chooseExisting(){const auto path=QFileDialog::getOpenFileName(this,tr("기존 SI 프로젝트 열기"),location_->text(),tr("SI Project (*.si_project.json *.json)"));if(!path.isEmpty())selectProject(path);}

void ProjectManagerDialog::createProject(){
  const auto name=name_->text().trimmed();
  const QRegularExpression invalid(R"([<>:"/\\|?*\x00-\x1f])");
  const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$))",QRegularExpression::CaseInsensitiveOption);
  if(name.isEmpty()||invalid.match(name).hasMatch()||reserved.match(name).hasMatch()||name.endsWith('.')||name==".."){
    QMessageBox::information(this,tr("프로젝트 이름"),tr("폴더 이름으로 사용할 수 있는 프로젝트 이름을 입력하세요."));name_->setFocus();return;
  }
  if(location_->text().trimmed().isEmpty()){location_->setFocus();return;}
  const QDir root(location_->text().trimmed());const auto directory=root.absoluteFilePath(name);const auto path=QDir(directory).filePath(name+".si_project.json");
  if(QFileInfo::exists(path)){QMessageBox::information(this,tr("이미 있는 프로젝트"),tr("같은 이름의 프로젝트가 있습니다. 기존 프로젝트를 열거나 다른 이름을 입력하세요."));return;}
  if(!QDir().mkpath(directory)){QMessageBox::warning(this,tr("프로젝트 생성 실패"),tr("프로젝트 폴더를 만들 수 없습니다."));return;}
  ProjectState state;state.energyBasis="5";state.projectName=name;state.baseDirectory=directory;state.siPath=QDir(directory).filePath(name+".docx");state.xyzPath=QDir(directory).filePath(name+".xyz");QString error;
  if(!ProjectStore::save(path,state,&error)){QMessageBox::warning(this,tr("프로젝트 생성 실패"),error);return;}
  settings().setValue("projectLibraryDirectory",root.absolutePath());selectProject(path);
}
