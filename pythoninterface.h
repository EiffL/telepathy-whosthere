/*
 * Copyright (C) 2013 Matthias Gehre <gehre.matthias@gmail.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef PYTHONINTERFACE_H
#define PYTHONINTERFACE_H

#include <thread>
#include <QObject>
#include <QString>
#include <boost/python.hpp>

/* Class which implements all signals emitted by yowsup */
class YowsupInterface : public QObject {
    Q_OBJECT
public:
    YowsupInterface(QObject *parent);
signals:
    void auth_success(QString mobilenumber);
    void auth_fail(QString mobilenumber, QString reason);
    void status_dirty();

    void message_received(QString msgId, QString jid, QString content, uint timestamp, bool wantsReceipt, QString pushName, bool isBroadcast);
    void image_received(QString msgId,QString jid,QString preview,QString url,QString size,bool wantsReceipt,bool isBroadcast);
    void video_received(QString msgId,QString jid,QString preview,QString url,QString size,bool wantsReceipt,bool isBroadcast);
    void audio_received(QString msgId,QString jid,QString url,QString size,bool wantsReceipt,bool isBroadcast);
    void location_received(QString msgId,QString jid,QString name,QString preview,QString latitude,QString longitude,bool wantsReceipt,bool isBroadcast);
    void vcard_received(QString msgId,QString jid,QString name, QString data,bool wantsReceipt,bool isBroadcast);

    void group_imageReceived(QString msgId,QString jid,QString author,QString preview,QString url,QString size,bool wantsReceipt);
    void group_videoReceived(QString msgId,QString jid,QString author,QString preview,QString url,QString size,bool wantsReceipt);
    void group_audioReceived(QString msgId,QString jid,QString author,QString url, QString size, bool wantsReceipt);
    void group_locationReceived(QString msgId,QString jid,QString author,QString name, QString preview,QString latitude, QString longitude, bool wantsReceipt);
    void group_vcardReceived(QString msgId,QString jid,QString author,QString name,QString data,bool wantsReceipt);
    void group_messageReceived(QString msgId,QString jid,QString author,QString content,int timestamp,bool wantsReceipt, QString pushName);

    void notification_contactProfilePictureUpdated(QString jid, uint timestamp,QString msgId, int pictureId, bool wantsReceipt);
    void notification_contactProfilePictureRemoved(QString jid, uint timestamp,QString msgId, bool wantsReceipt);
    void notification_groupParticipantAdded(QString gJid, QString jid, QString author, uint timestamp,QString msgId, bool wantsReceipt);
    void notification_groupParticipantRemoved(QString gjid, QString jid, QString author, uint timestamp,QString msgId,bool wantsReceipt);
    void notification_groupPictureUpdated(QString jid, QString author, uint timestamp, QString msgId, int pictureId, bool wantsReceipt);
    void notification_groupPictureRemoved(QString jid, QString author, uint timestamp, QString msgId, bool wantsReceipt);

    void disconnected(QString reason);
    void receipt_messageSent(QString jid,QString msgId);
    void receipt_messageDelivered(QString jid, QString msgId);
    void presence_updated(QString jid, QString lastSeen);
    void presence_available(QString jid);
    void presence_unavailable(QString jid);

    void group_subjectReceived(QString msgId,QString fromAttribute,QString author,QString newSubject,uint timestamp,bool receiptRequested);
    void profile_setStatusSuccess(QString jid, QString msgId);

    void group_setSubjectSuccess(QString jid);

    void group_gotInfo(QString jid,QString owner,QString subject,QString subjectOwner,qlonglong subjectT,qlonglong creation);

    void group_addParticipantsSuccess(QString jid, QString jids);
    void group_removeParticipantsSuccess(QString jid, QString jids);
    void group_createSuccess(QString jid);
    void group_createFail(QString errorCode);
    void group_endSuccess(QString jid);
    void group_gotPicture(QString jid, int pictureId, QString filepath);
    void group_infoError(QString errorCode);
    void group_gotParticipants(QString jid, QString jids);
    void group_setPictureSuccess(QString jid, int pictureId);
    void group_setPictureError(QString jid, QString errorCode);

    void profile_setPictureSuccess();
    void profile_setPictureError(QString errorCode);
    void receipt_visible(QString jid, QString msgId);
    void contact_gotProfilePictureId(QString jid, int pictureId, QString filename);
    void contact_typing(QString jid);
    void contact_paused(QString jid);
    void contact_gotProfilePicture(QString jid, QString filename);
    void message_error(QString msgId,QString jid,QString errorCode);
    void ping(QString pingId);
    void pong();
};

class PythonInterface
{
public:
    PythonInterface(YowsupInterface* handler);
    ~PythonInterface();
    /* Call a python function on Yowsup's methodInterface */
    template<typename... T>
    boost::python::object call(const QString& method, const T&... args);
    /* Call a python function in our python wrapper */
    template<typename... T>
    boost::python::object call_intern(const char* method, const T&... args);
    /* Runs the thread reading from the connection to whatsapp. Signals
     *  will be dispatched from that thread.
     */
    void runReaderThread();
    /* One-time initialization */
    static void initPython();
private:
    static boost::python::object pModule;
    boost::python::object pConnectionManager;
    std::thread readerThread;
};

/* Class to hold ensure/release GIL lock */
class GILStateHolder
{
public:
    GILStateHolder();
    ~GILStateHolder();
private:
    PyGILState_STATE gstate;
};

#endif // PYTHONINTERFACE_H
