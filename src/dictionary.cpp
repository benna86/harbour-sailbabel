#include "dictionary.hpp"
#include <QStandardPaths>
#include <QFile>
#include <QSet>
#include <QRegularExpression>
#include <QtSql/QSqlQuery>
#include <QQmlEngine>
#include <QUrl>
#include <QDir>
#include <QDebug>

dictionary::dictionary(QSqlQueryModel *parent) : QSqlQueryModel(parent) {
    _roleNames = new QHash<int,QByteArray>;
    _roleNames->insert(Qt::UserRole,      QByteArray("definition1"));
    _roleNames->insert(Qt::UserRole + 1,  QByteArray("definition2"));
    dicSize=0;
    dicProgress=0;
    lastTerm="";
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbName); // contains the list of language-specific databases
    if ( db.open ( )) {
        qDebug()<<"connected to "<<db.databaseName().toLatin1();
        QSqlQuery q;
        qDebug()<<"DB contains tables:";
        for (auto &a : db.tables(QSql::Tables)) {
            qDebug()<<a.toLatin1();
        }
        if(!db.tables(QSql::Tables).contains("dbnames")){
            qDebug()<<"initializing database";
            my_prepare(q,"create table dbnames (LID integer PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE,n varchar(250),l1 varchar(5),l2 varchar(5))");
            my_exec(q);
            dicts.clear();
        } else { //read dictionaries from database
            my_prepare(q,"SELECT n,l1,l2 FROM dbnames");
            my_exec(q);
            QVariant res;
            dbDescr dbdesc;
            if(q.first()){ //the record exists
                res=q.value(0);
                dbdesc.name=res.toString();
                qDebug()<<"Found table called "<<res.toString().toLatin1();
                res=q.value(1);
                dbdesc.lang1=res.toString();
                res=q.value(2);
                dbdesc.lang2=res.toString();
                dicts.push_back(dbdesc);
            } else { // no databases recorded yet
                dicts.clear();
                qDebug()<<"No tables found";
            }
            while(q.next()){
                res=q.value(0);
                dbdesc.name=res.toString();
                qDebug()<<"Found table called "<<res.toString().toLatin1();
                res=q.value(1);
                dbdesc.lang1=res.toString();
                res=q.value(2);
                dbdesc.lang2=res.toString();
                dicts.push_back(dbdesc);
            }
            emit sizeChanged();
        }
    } else {
        qDebug()<<"Impossible to connect to"<<dbName.toLatin1();
    }
}

QString dictionary::purify(const QString &entry) const {
  QString plain;
  plain.reserve(entry.size());
  bool in_word_mode=true;
  QChar waiting_for;
  for (auto l: entry) {
    if (in_word_mode) {
      if (l.isLetter()) {
        plain.append(l.toCaseFolded());
        continue;
      }
      if (l=='-')
        l=' ';
      if (l.isSpace() and (not plain.endsWith(' ')) and (not plain.isEmpty())) {
        plain.append(l);
        continue;
      }
      if (l=='(') {
        waiting_for=')'; in_word_mode=false; continue;
      }
      if (l=='[') {
        waiting_for=']'; in_word_mode=false; continue;
      }
      if (l=='{') {
        waiting_for='}'; in_word_mode=false; continue;
      }
      if (l=='<') {
        waiting_for='>'; in_word_mode=false; continue;
      }
    } else {
      if (l==waiting_for)
        in_word_mode=true;
    }
  }
  if (plain.endsWith(' '))
    plain.chop(1);
  return plain;
}

QString nameDictionary(QString l1, QString l2){
    l1.remove(QChar('"'));
    l2.remove(QChar('"'));
    l1.remove(QChar('\''));
    l2.remove(QChar('\''));
    l1.remove(QChar('`'));
    l2.remove(QChar('`'));
    return l1+"-"+l2;
}

void dictionary::read(const QString &filename) {
  QThread* thread=new QThread;
  dictionaryloader* worker=new dictionaryloader(*this, filename);
  worker->moveToThread(thread);
  connect(worker, SIGNAL(error(QString)), this, SLOT(error(QString)));
  connect(thread, SIGNAL(started()), worker, SLOT(process()));
  connect(worker, SIGNAL(finished()), thread, SLOT(quit()));
  connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
  connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
  connect(thread, SIGNAL(finished()), this, SLOT(threadFinished()));
  thread->start();
}

void dictionary::search(const QString dbname,const QString &term) {
    lastTerm=purify(term);
    updateModel(dbname,term);
}

QString dictionary::dictDbName(QString lang_A,QString lang_B){
    // update the dictionary list
    auto it=std::find_if(dicts.begin(), dicts.end(), [lang_A,lang_B](const dbDescr& s) {
        return (s.lang1==lang_A && s.lang2==lang_B);
    });
    QString dbname;
    if(it!=dicts.end()){
        qDebug()<<"Found match";
        dbname=it->name;
        // delete previously found dictionary
        //eraseDB(it->name);
    } else { //add the new dictionary
        qDebug()<<"Match not found";
        //update vector
        dbname=nameDictionary(lang_A,lang_B);
        dbDescr newdb;
        newdb.name=dbname;
        newdb.lang1=lang_A;
        newdb.lang2=lang_B;
        qDebug()<<"Inserting "<<newdb.name.toLatin1()<<","<<newdb.lang1.toLatin1()<<","<<newdb.lang2.toLatin1();
        dicts.push_back(newdb);
        //update list
        QSqlQuery d_ins;
        my_prepare(d_ins,"INSERT INTO dbnames(n, l1, l2) VALUES(:n,:l1,:l2)");
        d_ins.bindValue(":n",newdb.name);
        d_ins.bindValue(":l1",newdb.lang1);
        d_ins.bindValue(":l2",newdb.lang2);
        if(d_ins.exec()) {
            qDebug()<<"DB updated";
        } else {
            qDebug()<<"Cannot update DB: "<<d_ins.lastError().text().toLatin1();
        }
        // create tables
    initDB(dbname);
    }
    return dbname;
}

void dictionary::read_(const QString &filename) {
  map_A.clear();
  map_B.clear();
  QFile file(filename);
  if (not file.open(QIODevice::ReadOnly | QIODevice::Text))
    throw std::runtime_error("cannot read file");
    QString lang_A="";
    QString lang_B="";
    QString line(file.readLine());
    if (line.startsWith('#')){
        /* Checking for type of dictionary
       * assuming the first line of the dictionary contains a string like:
       * langA-langB vocabulary database  compiled by dict.cc
      */
        QRegularExpression re("(?<langA>[A-Z]+)-(?<langB>[A-Z]+)");
        QRegularExpressionMatch langs = re.match(line);
        if(langs.hasMatch()){
            lang_A=langs.captured("langA").toLatin1();
            lang_B=langs.captured("langB").toLatin1();
        }
    }
    emit sizeChanged();
    int cnt=0;
    if(lang_A.isEmpty() && lang_B.isEmpty()){
        qDebug()<<"Warning, unable to determine languages. Are languages described in the first line of the dictionary file?";
    } else {
        qDebug()<<"Processing dictionary "<<lang_A.toLatin1()+"-"+lang_B.toLatin1();
        QString dbname=dictDbName(lang_A,lang_B);
        qDebug()<<"Found dict "<<dbname.toLatin1();
        dicSize=0;
        dicProgress=0;
        // Prepare the queries, to be filled in later
        QSqlQuery q_ins_def,q_ins_word,q_ins_occ; //q_del_def,q_del_occ
        // Improve performance significantly by doing all operations in memory
        QSqlQuery("PRAGMA journal_mode = OFF");
        QSqlQuery("PRAGMA synchronous = OFF");
        //    q_del_def.prepare("DELETE FROM definitions WHERE lang1 = ? AND lang2 = ?");
        //    q_del_occ.prepare("DELETE FROM occurrences WHERE langFrom = ? AND langTo = ?");
        my_prepare(q_ins_def,"INSERT INTO `definitions"+dbname+"` (definition1,lang1,definition2,lang2) VALUES(:d1,:l1,:d2,:l2)");
        my_prepare(q_ins_word,"INSERT INTO `words"+dbname+"` (word, lang) VALUES (:w, :l)");
        my_prepare(q_ins_occ,"INSERT INTO `occurrences"+dbname+"` (langFrom,langTo,wordId,defId) VALUES (:l1,:l2,:w,:d)");
        QSqlDatabase::database().transaction();
        while (!file.atEnd()) {
            cnt++;
            line=file.readLine();
    if (line.startsWith('#'))
      continue;
#if QT_VERSION>=0x050400
    auto line_split=line.splitRef('\t');
#else
    auto line_split=line.split('\t');
#endif
    if (line_split.size()<2)
      continue;
            QString entry_A(line_split[0].toString());
            QString entry_B(line_split[1].toString());
    if (entry_A.startsWith("to "))
      entry_A.remove(0, 3);
    if (entry_B.startsWith("to "))
      entry_B.remove(0, 3);
    QString entry_plain_A=purify(entry_A);
    QString entry_plain_B=purify(entry_B);
            // Insert new terms
            q_ins_def.bindValue(":d1",entry_A);
            q_ins_def.bindValue(":l1",lang_A);
            q_ins_def.bindValue(":d2",entry_B);
            q_ins_def.bindValue(":l2",lang_B);
            my_exec(q_ins_def);
            // Update the map with the corresponding id, to keep track of what words have been already inserted
            int def_id=q_ins_def.lastInsertId().toInt();
            updateMap(map_A,entry_plain_A,def_id);
            updateMap(map_B,entry_plain_B,def_id);
            if (cnt%1000==0){
                dicProgress=cnt;
                emit sizeChanged();
    }
    }
        dicProgress=0;
        dicSize=int(map_A.size()+map_B.size());
        // Insert the data
        generateQuery(map_A,q_ins_word,q_ins_occ, lang_A, lang_A,lang_B);
        generateQuery(map_B,q_ins_word,q_ins_occ, lang_B, lang_A,lang_B);
        QSqlDatabase::database().commit();
      emit sizeChanged();
        // delete all old entries with the same languages
//        q_del_def.bindValue(0,lang_A);
//        q_del_def.bindValue(1,lang_B);
//        q_del_def.exec();
//        q_del_occ.bindValue(0,lang_A);
//        q_del_occ.bindValue(1,lang_B);
//        q_del_occ.exec();
  }
}

void dictionary::updateMap(QMultiHash<QByteArray, int> &map,QString entry,int def_id){
#if QT_VERSION>=0x050400
    for (const auto &v: entry.splitRef(' ', QString::SkipEmptyParts)) {
#else
    for (const auto &v: entry.split(' ', QString::SkipEmptyParts)) {
#endif
        QByteArray w=v.toUtf8();
        w.squeeze();
        map.insert(w, def_id);
    }
}

void dictionary::generateQuery(QMultiHash<QByteArray, int> map,QSqlQuery q_ins_word,QSqlQuery q_ins_occ,
                               QString lang,QString langFrom, QString langTo) {
    QString lastKey="";
    QList<int> values;
    values.clear();
    int word_id=-1;
    for (QMultiHash<QByteArray, int>::iterator i = map.begin(); i != map.end(); ++i){
        QString k=i.key();
        int v=i.value();
        if (dicProgress%1000==0){
            emit sizeChanged();
        }
        if(k!=lastKey){ // add a new word
            lastKey=k;
            values.clear();
            // insert word
            q_ins_word.bindValue(":w",k);
            q_ins_word.bindValue(":l",lang);
            my_exec(q_ins_word);
            word_id=q_ins_word.lastInsertId().toInt();
        }
        if(!values.contains(v)){ // not a duplicate
            values.append(v); //add to the list
            // insert occurrence
            if(v!=-1 and word_id!=-1){
                q_ins_occ.bindValue(":l1",langFrom);
                q_ins_occ.bindValue(":l2",langTo);
                q_ins_occ.bindValue(":w",word_id);
                q_ins_occ.bindValue(":d",v);
                my_exec(q_ins_occ);
            } else {
                qDebug()<<"Warning: invalid indexes";
            }
        } // if the entry is duplicated, do nothing
        dicProgress++;
    }
}

int dictionary::size() const {
    return dicSize;
}
int dictionary::progress() const {
    return dicProgress;
}

void dictionary::threadFinished() {
  emit readingFinished();
}

void dictionary::error(QString) {
  emit readingError();
}

void dictionary::eraseDB(QString dbname){
    // update the dictionary list
    auto it=std::find_if(dicts.begin(), dicts.end(), [dbname](const dbDescr& s) {
        return s.name==dbname;
    });
    if(it==dicts.end()){
        qDebug()<<"Warning: trying to erase an unknown dictionary: "<<dbname;
    } else { //erase the dictionary
        // update the list of dicts
        dicts.erase(it);
        // erase the tables in the dictionary
        QSqlQuery q;
        QStringList tabs=db.tables(QSql::Tables);
        for(QString n : tabs.filter(dbname)){
            qDebug()<<"Dropping table "<<n.toLatin1();
            q.clear();
            my_prepare(q,"DROP TABLE if exists `"+n+"`");
            my_exec(q);
        }
        // update the databases list
        q.clear();
        my_prepare(q,"DELETE FROM dbnames WHERE n = ?");
        q.bindValue(0,dbname);
        my_exec(q);
    }
    emit sizeChanged();
    //debug
    QStringList asd;
    for(auto &a : dicts){
        asd.push_back(a.name);
    }
    qDebug()<<"After erasing "<<asd;
}

void dictionary::initDB(QString dbname){
    QSqlQuery q;
    my_prepare(q,"create table `definitions"+dbname+"` (DID integer PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE,"
                       "definition1 varchar(250),"
                       "lang1 varchar(5), "
                       "definition2 varchar(250), "
                       "lang2 varchar(5))");
    my_exec(q);
    q.clear();
    my_prepare(q,"create table `words"+dbname+"` (WID integer PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE,"
                  "word varchar(250),"
                  "lang varchar(5)) ");
    my_exec(q);
    q.clear();
    my_prepare(q,"create table `occurrences"+dbname+"` (OID integer PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE, "
                  "langFrom varchar(5),"
                  "langTo varchar(5), "
                  "wordId INTEGER REFERENCES words(id), "
                  "defId INTEGER REFERENCES definitions(id)) ");
    my_exec(q);
    //debug
    QStringList asd;
    for(auto &a : dicts){
        asd.push_back(a.name);
    }
    qDebug()<<"After Init "<<asd;
}

void dictionary::updateModel(QString dbname,QString condition) {
    qDebug()<<"Searching cond "<<condition.toLatin1()<<" in DB "<<dbname.toLatin1();
    //QString dbname=nameDictionary(lang1,lang2);
    QString s;
    QStringList terms;
    QString sel_q="SELECT definition1,definition2 FROM `words"+dbname+"` w INNER JOIN `occurrences"+dbname+"` o ON o.wordId=w.wid INNER JOIN `definitions"+dbname+"` d ON o.defId = d.did WHERE word=?";
    if(condition.length()!=0){
        QStringList clauses=condition.split(' ', QString::SkipEmptyParts);
        if(clauses.length()>1){
            for(int i=0; i<clauses.length();i++){
                terms.append(sel_q);
            }
            s="WITH tbl AS ("+terms.join(" UNION ALL ")+") SELECT * FROM tbl GROUP BY definition1,definition2 HAVING COUNT(*)="+QString::number(clauses.length());
        } else {
            s=sel_q;
        }
        QSqlQuery q;
        my_prepare(q,s);
        for(int i=0;i<clauses.length();i++){
            q.bindValue(i,clauses[i]);
        }
        qDebug()<<"New query "<<q.lastQuery().toLatin1();
        my_exec(q);
        this->setQuery(q);
    }
}

//--------------------------------------------------------------------

dictionaryloader::dictionaryloader(dictionary &dict, const QString &filename) :
  dict(dict),
  filename(filename) {
}

void dictionaryloader::process() {
  try {
    dict.read_(filename);
    emit finished();
  }
  catch (...) {
    emit error("unable to read dictionary");
  }
}

sortingalgorithm::sortingalgorithm(dictionary *parent)
    : QSortFilterProxyModel(parent) {
    this->setSourceModel( parent );
    this->sort(0, Qt::DescendingOrder);
}

bool sortingalgorithm::lessThan(const QModelIndex &left,
                                const QModelIndex &right) const
{
    QStringList terms=QStringList()<<QString(sourceModel()->data(left).toString())<<QString(sourceModel()->data(right).toString());
    QStringList q_list=dynamic_cast<dictionary*>(sourceModel())->lastTerm.split(' ', QString::SkipEmptyParts);
    std::vector<int> scores;
    scores.resize(2);
    // compute score of both
    std::transform(terms.begin(),terms.end(),scores.begin(),[q_list](QString term){
        QString prefix=q_list[0];
        int score=0;
        if (term.startsWith(prefix)) {
            score+=6;
            if (QString(term).toCaseFolded().contains(QRegularExpression("^"+prefix+"\\S")))
                score-=2;
        } else if (term.contains(prefix))
            score+=3;
        for (int k=1; k<q_list.size(); ++k) {
            prefix+=" ";
            prefix+=q_list[k];
            if (term.startsWith(prefix))
                score+=6;
            else if (term.contains(prefix))
                score+=3;
        }
        // additional points if there is an exact match
        if (term==prefix)
            score+=2;
        // prefer short terms
        score-=term.count(" ");
        return score;
    });
    return scores[0]<scores[1];
}

void my_prepare(QSqlQuery &q, QString s){
    q.clear();
    if (!q.prepare(s)) {
        qDebug()<<q.executedQuery().toLatin1()<<";"<<q.lastError().text().toLatin1();
    }
}

void my_exec(QSqlQuery &q){
    if (!q.exec()) {
        qDebug()<<q.executedQuery().toLatin1()<<";"<<q.lastError().text().toLatin1();
    }
}
