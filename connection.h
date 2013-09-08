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

#pragma once

#include <tuple>
#include <QHash>
#include <TelepathyQt/BaseConnection>
#include <TelepathyQt/BaseChannel>
#include <boost/bimap.hpp>

#include "pythoninterface.h"

//There is no client with support for that
//#define USE_CAPTCHA_FOR_REGISTRATION

class YSConnection : public Tp::BaseConnection
{
    Q_OBJECT
    Q_DISABLE_COPY(YSConnection)
public:
    /* Implementation of BaseConnection */
    YSConnection( 	const QDBusConnection &  	dbusConnection,
                    const QString &  	cmName,
                    const QString &  	protocolName,
                    const QVariantMap &  	parameters);
    ~YSConnection();
    void connect(Tp::DBusError *error);
    QStringList inspectHandles(uint handleType, const Tp::UIntList& handles, Tp::DBusError *error);
    Tp::BaseChannelPtr createChannel(const QString& channelType, uint targetHandleType,
                                     uint targetHandle, Tp::DBusError *error);
    Tp::ContactAttributesMap getContactAttributes(const Tp::UIntList& handles,
                                                  const QStringList& interfaces,
                                                  Tp::DBusError *error);
    Tp::UIntList requestHandles(uint handleType, const QStringList& identifiers, Tp::DBusError* error);
    void messageAcknowledged(QString id);
    QString sendMessage(const QString& jid, const Tp::MessagePartList& message, uint flags, Tp::DBusError* error);
    uint setPresence(const QString& status, const QString& message, Tp::DBusError* error);
    Tp::ContactAttributesMap getContactListAttributes(const QStringList& interfaces, bool hold, Tp::DBusError* error);
    void requestSubscription(const Tp::UIntList& contacts, const QString& message, Tp::DBusError* error);


    void getContactsByVCardField(const QString& field, const QStringList& addresses,const QStringList& interfaces,
                                 Tp::AddressingNormalizationMap& addressingNormalizationMap,
                                 Tp::ContactAttributesMap& contactAttributesMap, Tp::DBusError* error);
    void getContactsByURI(const QStringList& URIs, const QStringList& interfaces,
                         Tp::AddressingNormalizationMap& addressingNormalizationMap,
                         Tp::ContactAttributesMap& contactAttributesMap, Tp::DBusError* error);
#ifdef USE_CAPTCHA_FOR_REGISTRATION
    /* Authentication */
    void getCaptchas(Tp::CaptchaInfoList& captchaInfoList, uint& numberRequired, QString& language, Tp::DBusError* error);
    QByteArray getCaptchaData(uint ID, const QString& mimeType, Tp::DBusError* error);
    void answerCaptchas(const Tp::CaptchaAnswers& answers, Tp::DBusError* error);
    void cancelCaptcha(uint reason, const QString& debugMessage, Tp::DBusError* error);
#endif
    void listRooms(Tp::BaseChannelRoomListTypePtr roomListType, Tp::DBusError* error);
private slots:
    void on_yowsup_auth_success(QString phonenumber);
    void on_yowsup_auth_fail(QString mobilenumber, QString reason);
    void on_yowsup_status_dirty();
    void on_yowsup_message_received(QString msgId, QString jid, QString content, uint timestamp, bool wantsReceipt, QString pushName, bool isBroadcast);
    void on_yowsup_disconnected(QString reason);
    void on_yowsup_receipt_messageSent(QString jid,QString msgId);
    void on_yowsup_receipt_messageDelivered(QString jid, QString msgId);
    void on_yowsup_presence_available(QString jid);
    void on_yowsup_presence_unavailable(QString jid);

    void on_yowsup_image_received(QString msgId,QString jid,QString preview,QString url,QString size,bool wantsReceipt,bool isBroadcast);
    void on_yowsup_video_received(QString msgId,QString jid,QString preview,QString url,QString size,bool wantsReceipt,bool isBroadcast);
    void on_yowsup_audio_received(QString msgId, QString jid, QString url, QString size, bool wantsReceipt,bool isBroadcast);
    void on_yowsup_location_received(QString msgId,QString jid,QString name,QString preview,QString latitude,QString longitude,bool wantsReceipt,bool isBroadcast);
    void on_yowsup_vcard_received(QString msgId,QString jid,QString name, QString data,bool wantsReceipt,bool isBroadcast);

    void on_yowsup_group_imageReceived(QString msgId,QString gid,QString jid,QString preview,QString url,QString size,bool wantsReceipt);
    void on_yowsup_group_videoReceived(QString msgId,QString gid,QString jid,QString preview,QString url,QString size,bool wantsReceipt);
    void on_yowsup_group_audioReceived(QString msgId,QString gid,QString jid,QString url, QString size, bool wantsReceipt);
    void on_yowsup_group_locationReceived(QString msgId,QString gid,QString jid,QString name, QString preview,QString latitude, QString longitude, bool wantsReceipt);
    void on_yowsup_group_vcardReceived(QString msgId,QString gid,QString jid,QString name,QString data,bool wantsReceipt);
    void on_yowsup_group_messageReceived(QString msgId,QString gid,QString jid,QString content, int timestamp, bool wantsReceipt, QString pushName);

    void on_yowsup_notification_contactProfilePictureUpdated(QString jid, uint timestamp,QString msgId,int pictureId, bool wantsReceipt);
    void on_yowsup_notification_contactProfilePictureRemoved(QString jid, uint timestamp,QString msgId, bool wantsReceipt);
    void on_yowsup_notification_groupParticipantAdded(QString gJid, QString jid, QString author, uint timestamp,QString msgId, bool wantsReceipt);
    void on_yowsup_notification_groupParticipantRemoved(QString gjid, QString jid, QString author, uint timestamp,QString msgId,bool wantsReceipt);
    void on_yowsup_notification_groupPictureUpdated(QString jid, QString author, uint timestamp, QString msgId, int pictureId, bool wantsReceipt);
    void on_yowsup_notification_groupPictureRemoved(QString jid, QString author, uint timestamp, QString msgId, bool wantsReceipt);

    void on_yowsup_group_subjectReceived(QString msgId,QString fromAttribute,QString author,QString newSubject,uint timestamp,bool receiptRequested);
    void on_yowsup_profile_setStatusSuccess(QString jid, QString msgId);
    void on_yowsup_group_gotInfo(QString gid, QString jid, QString subject, QString subjectOwner, qlonglong subjectT, qlonglong creation);
private:
    void yowsup_messageReceived(QString msgId, QString jid, const Tp::MessagePartList& body, uint timestamp,
                                bool wantsReceipt, const QString &gid = QString());
    void yowsup_linked_data_received(const char* type, QString msgId, QString jid, QString preview,
                                     QString url,QString size,bool wantsReceipt,const QString& gid = QString());
    void yowsup_vcard_received(QString msgId,QString jid,QString name, QString data,bool wantsReceipt, QString gid = QString());
    void yowsup_location_received(QString msgId, QString jid,
                                                QString name, QString preview,
                                                QString latitude, QString longitude,
                                                bool wantsReceipt, QString gid = QString());
private:
    bool isValidContact(const QString& identifier);
    bool isContactId(const QString& jid);
    bool isGroupId(const QString& jid);
    Tp::HandleType getType(uint handle);
    Tp::HandleType getType(const QString& id);
    bool isValidHandle(uint handle);
    uint getHandle(const QString& id);
    QString getIdentifier(uint handle);
    uint ensureHandle(QString id);
    uint addGroup(const QString& gid);
    uint ensureGroup(QString gid);
    uint addContact(const QString& jid);
    uint addContacts(const QStringList& jid);
    uint ensureContact(QString jid);
    void setPresenceState(const QList<uint> handles, const QString& status);
    void setSubscriptionState(const QStringList& jid, const QList<uint> handles, uint state);
    QString generateUID();
    QString formatSize(QString size_);
    Tp::SimplePresence getPresence(uint handle);
    Tp::BaseConnectionRequestsInterfacePtr requestsIface;
    Tp::BaseConnectionContactsInterfacePtr contactsIface;
    Tp::BaseConnectionSimplePresenceInterfacePtr simplePresenceIface;
    Tp::BaseConnectionContactListInterfacePtr contactListIface;
    Tp::BaseConnectionAddressingInterfacePtr addressingIface;
#ifdef USE_CAPTCHA_FOR_REGISTRATION
    /* Only valid during registration */
    Tp::BaseChannelCaptchaAuthenticationInterfacePtr captchaIface;
#endif
    /* Maps ids to identifiers. handle "0" is never valid according to spec */
    boost::bimap<uint,QString> mHandles;
    /* Maps a contact handle to its subscription state */
    QHash<uint,uint> mContactsSubscription;
    Tp::SimpleContactPresences mPresences;

    struct Room {
        QString id;
        QString owner;
        QString subject;
        QString subjectOwner;
        qlonglong subjectTimestamp;
        qlonglong creationTimestamp;
        QList<uint> members;
    };
    QHash<uint,Room> rooms;

    /* increasing id for unique telepathy-ids */
    uint lastMessageId;
    uint selfHandle;

    PythonInterface* pythonInterface;

    QString mPhoneNumber;
    QByteArray mPassword;
    YowsupInterface yowsupInterface;
};
