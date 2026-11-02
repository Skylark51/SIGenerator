#include "MolecularStyle.hpp"
#include <QJsonArray>
#include <QMap>
#include <QSettings>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace {
QColor baseColor(int z){
  static const QMap<int,QColor> colors={{0,QColor(180,180,180)},{1,QColor(245,245,245)},{2,QColor(217,255,255)},{3,QColor(204,128,255)},{4,QColor(194,255,0)},{5,QColor(255,181,181)},{6,QColor(70,70,70)},{7,QColor(48,80,248)},{8,QColor(235,40,40)},{9,QColor(144,224,80)},{10,QColor(179,227,245)},{11,QColor(171,92,242)},{12,QColor(138,255,0)},{13,QColor(191,166,166)},{14,QColor(240,200,160)},{15,QColor(255,128,0)},{16,QColor(255,200,50)},{17,QColor(31,240,31)},{18,QColor(128,209,227)},{19,QColor(143,64,212)},{20,QColor(61,255,0)},{21,QColor(230,230,230)},{22,QColor(191,194,199)},{23,QColor(166,166,171)},{24,QColor(138,153,199)},{25,QColor(156,122,199)},{26,QColor(224,102,51)},{27,QColor(240,144,160)},{28,QColor(80,208,80)},{29,QColor(200,128,51)},{30,QColor(125,128,176)},{31,QColor(194,143,143)},{32,QColor(102,143,143)},{33,QColor(189,128,227)},{34,QColor(255,161,0)},{35,QColor(166,41,41)},{36,QColor(92,184,209)},{53,QColor(148,0,148)},{78,QColor(208,208,224)},{79,QColor(255,209,35)},{80,QColor(184,184,208)}};return colors.value(z,QColor(180,180,180));
}
QVector3D vector(const QJsonObject& style,const char* key,QVector3D fallback){
  const auto a=style.value(key).toArray();if(a.size()==3)fallback={float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble())};
  if(fallback.lengthSquared()<1e-6f)fallback={-1,1,2};return fallback.normalized();
}
}
QJsonObject currentYoungcraftStyle(){
  QSettings saved(QSettings::NativeFormat,QSettings::UserScope,"Youngcraft","Youngcraft");saved.beginGroup("style");
  QJsonObject style{{"name",saved.value("name","Youngcraft Studio").toString()}};
  const bool hasSaved=saved.contains("name");
  const QJsonObject numbers{{"atomScale",0.28},{"bondRadius",0.032},{"ambientIntensity",0.54},{"diffuseIntensity",1.215},
    {"specularIntensity",0.95},{"shininess",120},{"atomSheenStrength",0.08},{"colorSaturation",1.0},{"bondColorMode",0},
    {"opacity",1.0},{"bondOpacity",1.0},{"displayMode",0},{"spaceFillingScale",0.72},{"depthCueStrength",0.35}};
  for(auto it=numbers.begin();it!=numbers.end();++it)style.insert(it.key(),saved.value(it.key(),it.value().toDouble()).toDouble());
  for(const auto& key:{"lighting","twoLights","cameraRelativeLights","perspective","monochrome","depthCueing"}){
    const bool fallback=QString(key)=="lighting"||QString(key)=="cameraRelativeLights";
    style.insert(key,saved.value(key,fallback).toBool());
  }
  const QMap<QString,QColor> colors={{"background",Qt::white},{"bondColor",QColor(160,166,178)},{"labelColor",QColor(35,38,44)},{"lightColor",Qt::white},{"secondLightColor",Qt::white}};
  for(auto it=colors.begin();it!=colors.end();++it){auto color=saved.value(it.key(),it.value()).value<QColor>();if(!color.isValid())color=it.value();style.insert(it.key(),color.name(QColor::HexArgb));}
  const QMap<QString,QVector3D> directions={{"lightDirection",QVector3D(-1,1.6f,2.4f)},{"secondLightDirection",QVector3D(1.6f,-0.8f,2)}};
  for(auto it=directions.begin();it!=directions.end();++it){const auto v=saved.value(it.key(),QVariant::fromValue(it.value())).value<QVector3D>();style.insert(it.key(),QJsonArray{v.x(),v.y(),v.z()});}
  QJsonObject palette,radii;
  // Complete fallback palette from Youngcraft's element table.
  for(int z=1;z<=118;++z)palette.insert(QString::number(z),baseColor(z).name(QColor::HexArgb));
  if(!hasSaved){
    const QMap<int,QColor> studio={{1,QColor(250,250,250)},{6,QColor(12,12,12)},{7,QColor(0,0,255)},{8,QColor(255,0,0)},
      {9,QColor(145,220,175)},{16,QColor(255,220,25)},{17,QColor(155,220,165)},{25,QColor(185,55,245)},{26,QColor(255,160,30)},{27,QColor(140,65,225)}};
    for(auto it=studio.begin();it!=studio.end();++it)palette.insert(QString::number(it.key()),it.value().name(QColor::HexArgb));
    radii={{"1",0.76},{"6",0.75},{"7",1.08},{"8",0.98}};
  }
  saved.beginGroup("elementColors");for(const auto& key:saved.childKeys()){const auto c=saved.value(key).value<QColor>();if(c.isValid())palette.insert(key,c.name(QColor::HexArgb));}saved.endGroup();
  saved.beginGroup("elementRadiusScales");for(const auto& key:saved.childKeys())radii.insert(key,saved.value(key).toDouble());saved.endGroup();
  style.insert("elementColors",palette);style.insert("elementRadiusScales",radii);return style;
}
QColor molecularElementColor(int z,const QJsonObject& style){
  if(style.value("monochrome").toBool())return QColor(190,190,190);
  const auto c=QColor(style.value("elementColors").toObject().value(QString::number(z)).toString());return c.isValid()?c:baseColor(z);
}
QImage molecularSphere(const QColor& color,const QJsonObject& style){
  // Youngcraft's gamma-correct hemisphere, key/fill diffuse, and neutral
  // Blinn-Phong highlight, sampled on a sphere for the portable painter view.
  constexpr int side=192;
  QImage image(side,side,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
  const QVector3D key=vector(style,"lightDirection",{-1,1.6f,2.4f}),fill=vector(style,"secondLightDirection",{1.6f,-0.8f,2});
  const auto halfKey=(key+QVector3D(0,0,1)).normalized(),halfFill=(fill+QVector3D(0,0,1)).normalized();
  const auto lightColor=QColor(style.value("lightColor").toString("#ffffffff")),secondColor=QColor(style.value("secondLightColor").toString("#ffffffff"));
  double base[3]={color.redF(),color.greenF(),color.blueF()},lc[3]={lightColor.redF(),lightColor.greenF(),lightColor.blueF()},fc[3]={secondColor.redF(),secondColor.greenF(),secondColor.blueF()};
  const double luminance=base[0]*0.2126+base[1]*0.7152+base[2]*0.0722,saturation=style.value("colorSaturation").toDouble(1);
  for(int i=0;i<3;++i){base[i]=std::pow(std::clamp(luminance+(base[i]-luminance)*saturation,0.0,1.0),2.2);lc[i]=std::pow(lc[i],2.2);fc[i]=std::pow(fc[i],2.2);}
  const double ambient=style.value("ambientIntensity").toDouble(0.54),diffuse=style.value("diffuseIntensity").toDouble(1.215),specular=style.value("specularIntensity").toDouble(0.95),shine=std::max(1.0,style.value("shininess").toDouble(120)),sheen=style.value("atomSheenStrength").toDouble(0.08);
  const bool lighting=style.value("lighting").toBool(true),two=style.value("twoLights").toBool(false);
  const int alpha=qRound(255*std::clamp(style.value("opacity").toDouble(1),0.0,1.0));
  for(int y=0;y<side;++y)for(int x=0;x<side;++x){
    const double nx=(x+0.5)*2/side-1,ny=1-(y+0.5)*2/side,r2=nx*nx+ny*ny;if(r2>=1)continue;
    const QVector3D normal(float(nx),float(ny),float(std::sqrt(1-r2)));
    const double kd=std::max(0.0f,QVector3D::dotProduct(normal,key)),fd=std::max(0.0f,QVector3D::dotProduct(normal,fill));
    const double kh=std::max(0.0f,QVector3D::dotProduct(normal,halfKey)),fh=std::max(0.0f,QVector3D::dotProduct(normal,halfFill));
    const double ks=kd>0?std::pow(kh,shine)*specular:0,fs=two&&fd>0?std::pow(fh,shine)*specular*0.12:0,soft=kd>0?std::pow(kh,12)*sheen*specular:0;
    int rgb[3];for(int i=0;i<3;++i){double value=base[i];if(lighting)value=base[i]*(ambient*(0.8+0.2*ny)+lc[i]*kd*diffuse+(two?fc[i]*fd*diffuse*0.28:0))+lc[i]*(ks+soft)+fc[i]*fs;rgb[i]=qRound(255*std::pow(std::clamp(value,0.0,1.0),1/2.2));}
    image.setPixel(x,y,qPremultiply(qRgba(rgb[0],rgb[1],rgb[2],alpha)));
  }
  return image;
}
