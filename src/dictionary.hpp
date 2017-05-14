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
#include <QSqlQueryModel>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QSqlError>

class dictionary;
class dictionaryloader;
void my_prepare(QSqlQuery &q, QString s);
void my_exec(QSqlQuery &q);

class dictionary : public QSqlQueryModel {
  Q_OBJECT
  QVector<QByteArray> dict_A;
  QVector<QByteArray> dict_B;
  QMultiHash<QByteArray, int> map_A;
  QMultiHash<QByteArray, int> map_B;
  int dicSize;
  int dicProgress;
  int max_num_results=200;
  friend class dictionaryloader;
  QHash<int,QByteArray> *_roleNames;

public:
  Q_PROPERTY(int size READ size NOTIFY sizeChanged)
  Q_PROPERTY(int progress READ progress NOTIFY sizeChanged)
  Q_PROPERTY(QStringList langs READ getLangs NOTIFY sizeChanged)
  explicit dictionary(QSqlQueryModel *parent = 0);
private:
  QString purify(const QString &entry) const;
  void generateQuery(QMultiHash<QByteArray, int> map,QSqlQuery q_ins_word,QSqlQuery q_ins_occ,QString lang,QString langFrom, QString langTo);
  void updateMap(QMultiHash<QByteArray, int> &map,QString entry,int def_id);
  QSqlDatabase db;
public:
  Q_INVOKABLE void read(const QString &filename);
  Q_INVOKABLE void eraseDB(QString dbname);
  Q_INVOKABLE void initDB(QString dbname);
  Q_INVOKABLE void clear(){this->setQuery("select * from words where id=-1");}
  Q_INVOKABLE void search(const QString lang,const QString &term);
private:
  void read_(const QString &filename);
  struct dbDescr {
    QString name;
    QString lang1;
    QString lang2;
  };
  QVector<dbDescr> dicts;
  QString dbName="sailbabelDB";
  QString dictDbName(QString lang_A,QString lang_B);
public:
  int size() const;
  int progress() const;
  QStringList getLangs() {QStringList ret; for(auto a: dicts) {ret<<a.name;} return ret;}
  virtual ~dictionary() {if (db.open()) db.close();}
  QVariant data(const QModelIndex &index, int role) const {
      if(role < Qt::UserRole) {
         return QSqlQueryModel::data(index, role);
      }
      QSqlRecord r = record(index.row());
      return r.value(QString(_roleNames->value(role))).toString();
   }
  inline QHash<int, QByteArray> roleNames() const { return *_roleNames; }
  void updateModel(QString dbname,QString condition);
signals:
  void sizeChanged();
  void readingFinished();
  void readingError();
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
