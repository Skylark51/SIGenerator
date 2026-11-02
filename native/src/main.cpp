#include "MainWindow.hpp"
#include "ProjectManagerDialog.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSplashScreen>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QTimer>

namespace {
QPixmap startupArtwork(qreal scale) {
  QPixmap artwork(qRound(520 * scale), qRound(340 * scale));
  artwork.setDevicePixelRatio(scale);
  artwork.fill(Qt::transparent);
  QPainter painter(&artwork);
  painter.setRenderHint(QPainter::Antialiasing);
  // Leave a transparent margin around the softly rounded, double-line frame.
  for (int spread = 6; spread >= 1; --spread) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 5, 18, 5 + (6 - spread) * 2));
    painter.drawRoundedRect(QRectF(7 - spread, 8 - spread, 506 + spread * 2,
                                  324 + spread * 2), 24 + spread, 24 + spread);
  }
  QLinearGradient background(0, 0, 520, 340);
  background.setColorAt(0, QColor("#102d50"));
  background.setColorAt(1, QColor("#06162c"));
  QLinearGradient frame(12, 8, 508, 332);
  frame.setColorAt(0, QColor("#78c9d6"));
  frame.setColorAt(0.45, QColor("#345776"));
  frame.setColorAt(1, QColor("#597c9c"));
  painter.setPen(QPen(QBrush(frame), 1));
  painter.setBrush(background);
  painter.drawRoundedRect(QRectF(7.5, 7.5, 505, 325), 24, 24);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(164, 210, 233, 35), 1));
  painter.drawRoundedRect(QRectF(12.5, 12.5, 495, 315), 19, 19);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.drawPixmap(QRect(184, 30, 152, 152), QPixmap(":/si/si-generator.png"));
  // Draw the wordmark as vectors so its fine curves stay crisp on HiDPI screens.
  QFont initials(QStringLiteral("Segoe UI"));
  initials.setPixelSize(48);
  initials.setWeight(QFont::Bold);
  initials.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
  QPainterPath wordmark;
  wordmark.addText(QPointF(0, 0), initials, QStringLiteral("SI"));
  QFont name(QStringLiteral("Georgia"));
  name.setPixelSize(43);
  name.setItalic(true);
  wordmark.addText(QPointF(wordmark.boundingRect().right() + 14, 0), name,
                   QStringLiteral("Generator"));
  const QRectF bounds = wordmark.boundingRect();
  painter.save();
  painter.translate(260 - bounds.center().x(), 211 - bounds.center().y());
  QLinearGradient lettering(bounds.topLeft(), bounds.bottomRight());
  lettering.setColorAt(0, QColor("#72e6e1"));
  lettering.setColorAt(0.35, QColor("#d5f9f5"));
  lettering.setColorAt(0.7, QColor("#f3f8ff"));
  lettering.setColorAt(1, QColor("#a9c7f5"));
  painter.translate(0, 2);
  painter.fillPath(wordmark, QColor(0, 5, 18, 110));
  painter.translate(0, -2);
  painter.fillPath(wordmark, lettering);
  painter.restore();
  QFont detail(QStringLiteral("Segoe UI"));
  detail.setPixelSize(13);
  detail.setWeight(QFont::Light);
  detail.setLetterSpacing(QFont::AbsoluteSpacing, 0.9);
  painter.setFont(detail);
  painter.setPen(QColor("#adc5dd"));
  painter.drawText(QRect(24, 237, 472, 24), Qt::AlignCenter,
                   QStringLiteral("Tool For Making Supporting Information"));
  painter.setBrush(QColor("#41c9df"));
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(QRectF(230, 282, 60, 3), 1.5, 1.5);
  QFont status = QApplication::font();
  status.setPixelSize(12);
  painter.setFont(status);
  painter.setPen(QColor("#adc5dd"));
  painter.drawText(QRect(24, 300, 472, 22), Qt::AlignCenter,
                   QStringLiteral("시작하는 중…"));
  return artwork;
}

// QSplashScreen normally closes on a click; keep the startup transition in control.
class StartupSplash final : public QSplashScreen {
public:
  explicit StartupSplash(QScreen* screen)
      : QSplashScreen(screen, startupArtwork(screen->devicePixelRatio())) {
    setObjectName(QStringLiteral("startupSplash"));
    setWindowTitle(QStringLiteral("SI Generator — 시작"));
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlag(Qt::FramelessWindowHint);
  }
protected:
  void paintEvent(QPaintEvent*) override {
    // Paint only the alpha artwork; QSplashScreen's default background fill
    // would turn the transparent corners into a rectangular window.
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap());
  }
  void mousePressEvent(QMouseEvent*) override {}
};
}

int main(int argc, char* argv[]) {
  QSurfaceFormat format;format.setSamples(4);format.setDepthBufferSize(24);format.setStencilBufferSize(8);QSurfaceFormat::setDefaultFormat(format);
  QApplication application(argc,argv);application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  application.setStyleSheet(QStringLiteral(R"(
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
  )"));
  application.setWindowIcon(QIcon(":/si/si-generator.png"));application.setQuitOnLastWindowClosed(false);
  StartupSplash splash(application.primaryScreen());
  splash.show();
  QElapsedTimer startupElapsed;
  startupElapsed.start();
  application.processEvents();
  MainWindow window;ProjectManagerDialog manager;bool editing=false;
  QObject::connect(&manager,&QDialog::accepted,&application,[&]{
    if(window.loadProjectPath(manager.selectedProject())){editing=true;window.show();window.raise();window.activateWindow();}
    else {manager.refreshProjects();manager.show();}
  });
  QObject::connect(&manager,&QDialog::rejected,&application,[&]{if(editing){window.show();window.raise();window.activateWindow();}else application.quit();});
  QObject::connect(&window,&MainWindow::projectManagerRequested,&application,[&]{
    if(!window.saveActiveProject())return;window.hide();manager.refreshProjects();manager.show();manager.raise();manager.activateWindow();
  });
  QObject::connect(&window,&MainWindow::editorClosed,&application,&QApplication::quit);
  // Include initialization and the fade in the two-second startup interval.
  // A precise timer prevents Qt's coarse timer from firing early.
  constexpr int startupDurationMs = 2000;
  constexpr int transitionDurationMs = 240;
  const int remainingDelayMs = qMax(0, startupDurationMs - transitionDurationMs
                                      - int(startupElapsed.elapsed()));
  QTimer::singleShot(remainingDelayMs, Qt::PreciseTimer, &splash, [&]{
    auto* transition = new QParallelAnimationGroup(&splash);
    auto* fade = new QPropertyAnimation(&splash, "windowOpacity", transition);
    fade->setDuration(transitionDurationMs);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InOutQuad);
    auto* slide = new QPropertyAnimation(&splash, "pos", transition);
    slide->setDuration(transitionDurationMs);
    slide->setStartValue(splash.pos());
    slide->setEndValue(splash.pos() + QPoint(0, 12));
    slide->setEasingCurve(QEasingCurve::InOutQuad);
    QObject::connect(transition, &QParallelAnimationGroup::finished, &manager, [&]{
      manager.show();
      splash.close();
      manager.raise();
      manager.activateWindow();
    });
    transition->start(QAbstractAnimation::DeleteWhenStopped);
  });
  return application.exec();
}
