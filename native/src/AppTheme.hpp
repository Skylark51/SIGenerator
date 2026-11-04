#pragma once
#include <QApplication>
#include <QSettings>
#include <QString>
#include <algorithm>

inline int siStoredTheme() {
  QSettings prefs(QStringLiteral("SI Generator"),
    qEnvironmentVariableIsSet("SI_GENERATOR_TEST_SETTINGS")?QStringLiteral("SIGeneratorTests"):QStringLiteral("SIGenerator"));
  return std::clamp(prefs.value(QStringLiteral("projectManagerTheme"),0).toInt(),0,2);
}
inline QString siEditorStyle(int index) {
  const int theme=std::clamp(index,0,2);
  QString css=QStringLiteral(R"SI_STYLE(
    QWidget{color:#334155;font-size:9pt} QMainWindow,QScrollArea,#quickToolbar{background:#f4f7fb}
    QGroupBox{background:#fff;border:1px solid #d8e1ec;border-radius:8px;margin-top:12px;padding:10px 7px 7px} QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 5px;color:#52647a;font-weight:600}
    QLineEdit,QTextEdit,QComboBox,QSpinBox,QTableWidget{background:#fff;border:1px solid #cbd7e5;border-radius:7px;padding:5px;selection-background-color:#b8d8ff} QLineEdit:focus,QTextEdit:focus,QComboBox:focus,QSpinBox:focus{border:1px solid #7aaee8}
    QPushButton,QToolButton{background:#e4edf7;border:1px solid #bfd0e3;border-radius:8px;padding:7px;color:#34495e} QPushButton:hover,QToolButton:hover{background:#d6e5f5} QPushButton:pressed,QToolButton:pressed{background:#c7dcef} QPushButton:disabled,QToolButton:disabled{background:#edf1f5;color:#9aa7b5;border-color:#dde3ea}
    QToolButton[tone="energy"]{background:#e4efff;border-color:#b9d3f4} QToolButton[tone="spin"]{background:#e8f6ed;border-color:#bcdcc7} QToolButton[tone="geometry"]{background:#fff0dc;border-color:#ead0a8} QToolButton[tone="view"]{background:#eee9fb;border-color:#d3c5ed} QToolButton[tone="generate"]{background:#dceef5;border-color:#acd0df;font-weight:600}
    QToolBar#quickToolbar{background:#fff;border:0;border-bottom:1px solid #d8e1ec;padding:5px;spacing:4px}
    QToolBar#quickToolbar QToolButton{background:transparent;border:1px solid transparent;border-radius:7px;padding:4px}
    QToolBar#quickToolbar QToolButton:hover{background:#edf3fb;border-color:#c4d6ec}
    QToolBar#quickToolbar QToolButton:checked{background:#dceaff;border-color:#7da6db}
    QToolBar#quickToolbar QToolButton:focus{border-color:#387bc3}
    QToolBar::separator{background:#dbe3ee;width:1px;margin:7px 5px}
    QLabel#toolbarBrand{font-size:12pt;font-weight:600;color:#24486c}
    QLabel#currentSectionLabel{padding:0 10px;color:#52647a;font-weight:600}
    QCheckBox{spacing:7px} QHeaderView::section{background:#e8eef6;border:0;border-bottom:1px solid #c7d3e1;padding:6px;font-weight:600} QSplitter::handle{background:#dce5ef}
  
    QLabel#projectHeading, QLabel#structureScope {font-weight:600;font-size:12pt;color:#24486c}
)SI_STYLE");
  css.replace(QStringLiteral("#f4f7fb"),QStringLiteral(theme==0?"#f1f7fd":theme==1?"#f0f8f3":"#f8f4fc"));
  css.replace(QStringLiteral("#e4edf7"),QStringLiteral(theme==0?"#e4edf7":theme==1?"#e4f3eb":"#f0e9fa"));
  css.replace(QStringLiteral("#d6e5f5"),QStringLiteral(theme==0?"#d6e5f5":theme==1?"#d4eadd":"#e8dbf6"));
  css.replace(QStringLiteral("#c7dcef"),QStringLiteral(theme==0?"#c7dcef":theme==1?"#c4e1d2":"#dbccf1"));
  css.replace(QStringLiteral("#dceaff"),QStringLiteral(theme==0?"#dceaff":theme==1?"#d7efe1":"#e8def8"));
  css.replace(QStringLiteral("#7da6db"),QStringLiteral(theme==0?"#7da6db":theme==1?"#64ad8c":"#a78bd4"));
  css.replace(QStringLiteral("#387bc3"),QStringLiteral(theme==0?"#387bc3":theme==1?"#287466":"#7552a8"));
  css.replace(QStringLiteral("#24486c"),QStringLiteral(theme==0?"#24486c":theme==1?"#235e53":"#614187"));
  css.replace(QStringLiteral("#b8d8ff"),QStringLiteral(theme==0?"#b8d8ff":theme==1?"#c1e8d1":"#e2cdf7"));
  css.replace(QStringLiteral("#7aaee8"),QStringLiteral(theme==0?"#7aaee8":theme==1?"#5ba88c":"#9876ce"));
  return css;
}
inline void applySiApplicationTheme(int index) {
  if(auto* app=qobject_cast<QApplication*>(QApplication::instance()))
    app->setStyleSheet(siEditorStyle(index));
}
