#ifndef DICTIONARY_HPP
#define DICTIONARY_HPP

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QMultiHash>
#include <QVariantList>
#include "sqlquerymodel.h"

class dictionary;
class dictionaryloader;

class dictionary : public QObject {
  Q_OBJECT
  QString lang_A;
  QString lang_B;
  int dicSize;
  int max_num_results=200;
  friend class dictionaryloader;

public:
  Q_PROPERTY(int size READ size NOTIFY sizeChanged)
  explicit dictionary(QObject *parent = 0);
  Q_PROPERTY(QString langFrom READ langFrom NOTIFY sizeChanged)
  Q_PROPERTY(QString langTo READ langTo NOTIFY sizeChanged)
private:
  QString purify(const QString &entry) const;
  void generateQuery(QSqlQuery q_sel_word,QSqlQuery q_ins_word,QSqlQuery q_ins_occ,QString entry,QString lang,QString langFrom, QString langTo,int def_id);
public:
  Q_INVOKABLE void read(const QString &filename);
private:
  void read_(const QString &filename);
public:
  int size() const;
  QString langFrom() const {return lang_A;}
  QString langTo() const {return lang_B;}
  virtual ~dictionary() {}
  void openDB(QUrl offlineStoragePath,QString dbname);
signals:
  void sizeChanged();
  void dictChanged();
  void readingFinished();
  void readingError();
  void initDB();
public slots:
  void threadFinished();
  void error(QString err);
};

//---------------------------------------------------------------------

class dictionaryloader : public QObject {
  Q_OBJECT
  dictionary &dict;
  QString filename;
public:
  dictionaryloader(dictionary &dict, const QString &filename);
  virtual ~dictionaryloader() { }
public slots:
  void process();
signals:
  void finished();
  void error(QString err);
};

#endif // DICTIONARY_HPP
