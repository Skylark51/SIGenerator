#include <QApplication>
#include <QPainter>
#include <QPainterPath>
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
  painter.drawPixmap(QRect(184, 30, 152, 152), QPixmap("S:/native/resources/si-generator.png"));
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


int main(int argc,char** argv){QApplication app(argc,argv);return startupArtwork(1).save("S:/tmp/splash-preview.png") && startupArtwork(2).save("S:/tmp/splash-preview-2x.png") ? 0 : 1;}