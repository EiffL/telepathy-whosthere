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

#include <algorithm>
#include <QDebug>
#include <TelepathyQt/Constants>
#include "connection.h"
#include "protocol.h"

using namespace Tp;
using namespace std;
namespace python = boost::python;

YSConnection::YSConnection( const QDBusConnection &  	dbusConnection,
                            const QString &  	cmName,
                            const QString &  	protocolName,
                            const QVariantMap &  	parameters
                            ) : BaseConnection(dbusConnection, cmName, protocolName, parameters),
                                lastMessageId(1),
                                yowsupInterface(this)
{
    qDebug() << "YSConnection::YSConnection proto: " << protocolName
             << " parameters: " << parameters;
    if(parameters.contains("account"))
        mPhoneNumber = parameters["account"].toString();
    if(parameters.contains("password"))
        mPassword = QByteArray::fromBase64( parameters["password"].toString().toLatin1() );

    selfHandle = addContact(mPhoneNumber + "@s.whatsapp.net");
    assert(selfHandle == 1);

    setSelfHandle(selfHandle);

    setConnectCallback( Tp::memFun(this,&YSConnection::connect) );
    setInspectHandlesCallback( Tp::memFun(this,&YSConnection::inspectHandles) );
    setCreateChannelCallback( Tp::memFun(this,&YSConnection::createChannel) );
    setRequestHandlesCallback( Tp::memFun(this,&YSConnection::requestHandles) );

    /* Connection.Interface.Requests */
    requestsIface = BaseConnectionRequestsInterface::create(this);
    /* Fill requestableChannelClasses */
    RequestableChannelClass text;
    text.fixedProperties[TP_QT_IFACE_CHANNEL+".ChannelType"] = TP_QT_IFACE_CHANNEL_TYPE_TEXT;
    text.fixedProperties[TP_QT_IFACE_CHANNEL+".TargetHandleType"]  = HandleTypeContact;
    text.allowedProperties.append(TP_QT_IFACE_CHANNEL+".TargetHandle");
    text.allowedProperties.append(TP_QT_IFACE_CHANNEL+".TargetID");
    RequestableChannelClass textRoom;
    textRoom.fixedProperties[TP_QT_IFACE_CHANNEL+".ChannelType"] = TP_QT_IFACE_CHANNEL_TYPE_TEXT;
    textRoom.fixedProperties[TP_QT_IFACE_CHANNEL+".TargetHandleType"]  = HandleTypeRoom;
    textRoom.allowedProperties.append(TP_QT_IFACE_CHANNEL+".TargetHandle");
    textRoom.allowedProperties.append(TP_QT_IFACE_CHANNEL+".TargetID");
    RequestableChannelClass roomList;
    roomList.fixedProperties[TP_QT_IFACE_CHANNEL+".ChannelType"] = TP_QT_IFACE_CHANNEL_TYPE_ROOM_LIST;
    roomList.fixedProperties[TP_QT_IFACE_CHANNEL+".TargetHandleType"]  = HandleTypeNone;
    requestsIface->requestableChannelClasses << text << textRoom << roomList;
    plugInterface(AbstractConnectionInterfacePtr::dynamicCast(requestsIface));

    /* Connection.Interface.Contacts */
    contactsIface = BaseConnectionContactsInterface::create();
    contactsIface->setGetContactAttributesCallback(Tp::memFun(this,&YSConnection::getContactAttributes));
    contactsIface->setContactAttributeInterfaces(QStringList()
                                                 << QLatin1String("org.freedesktop.Telepathy.Connection")
                                                 << QLatin1String("org.freedesktop.Telepathy.Connection.Interface.ContactList")
                                                 << TP_QT_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE);
    plugInterface(AbstractConnectionInterfacePtr::dynamicCast(contactsIface));

    /* Connection.Interface.SimplePresence */
    simplePresenceIface = BaseConnectionSimplePresenceInterface::create();
    simplePresenceIface->setSetPresenceCallback(Tp::memFun(this,&YSConnection::setPresence));
    plugInterface(AbstractConnectionInterfacePtr::dynamicCast(simplePresenceIface));

    /* Connection.Interface.ContactList */
    contactListIface = BaseConnectionContactListInterface::create();
    contactListIface->setGetContactListAttributesCallback(Tp::memFun(this,&YSConnection::getContactListAttributes));
    contactListIface->setRequestSubscriptionCallback(Tp::memFun(this,&YSConnection::requestSubscription));
    plugInterface(AbstractConnectionInterfacePtr::dynamicCast(contactListIface));

    /* Connection.Interface.Adressing */
    addressingIface = BaseConnectionAddressingInterface::create();
    addressingIface->setGetContactsByVCardFieldCallback( Tp::memFun(this,&YSConnection::getContactsByVCardField) );
    addressingIface->setGetContactsByURICallback( Tp::memFun(this,&YSConnection::getContactsByURI) );
    plugInterface(AbstractConnectionInterfacePtr::dynamicCast(addressingIface));

    /* Python interface to yowsup */
    pythonInterface = new PythonInterface(&yowsupInterface);
    yowsupInterface.setObjectName("yowsup");
    QMetaObject::connectSlotsByName(this);
}

YSConnection::~YSConnection() {
    delete pythonInterface;
}

/* I wanted one connection per account, but the account manager
 * does not close failed connections under some circumstances (e.g. failed auth)
QString YSConnection::uniqueName() const {
    //May not start with a digit
    return "u" + mPhoneNumber;
}
*/

void YSConnection::connect(Tp::DBusError *error) {
    qDebug() << "Thread id connect " << QThread::currentThreadId();
    qDebug() << "YSConnection::connect";
    setStatus(ConnectionStatusConnecting, ConnectionStatusReasonRequested);

#ifdef USE_CAPTCHA_FOR_REGISTRATION
    /* Clear pointer from a previous registration */
    captchaIface.reset();
#endif

    if(mPassword.length() > 0 ) {
        pythonInterface->call("auth_login", mPhoneNumber, mPassword );
    } else {
#ifdef USE_CAPTCHA_FOR_REGISTRATION
        qDebug() << "Opening registration";
        if( mUID.length() == 0 ) {
            qDebug() << "Generating new mUID";
            mUID = generateUID();
            //new SetAccountProperties();
        }
        //Registration
        BaseChannelPtr baseChannel = BaseChannel::create(this, TP_QT_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
                                                         0, HandleTypeNone, "", false, 0, "");

        BaseChannelServerAuthenticationTypePtr authType
                = BaseChannelServerAuthenticationType::create(TP_QT_IFACE_CHANNEL_INTERFACE_CAPTCHA_AUTHENTICATION);
        baseChannel->plugInterface(AbstractChannelInterfacePtr::dynamicCast(authType));

        captchaIface = BaseChannelCaptchaAuthenticationInterface::create(true);
        captchaIface->setGetCaptchaDataCallback( Tp::memFun(this, &YSConnection::getCaptchaData));
        captchaIface->setGetCaptchasCallback( Tp::memFun(this, &YSConnection::getCaptchas));
        captchaIface->setAnswerCaptchasCallback( Tp::memFun(this, &YSConnection::answerCaptchas));
        captchaIface->setCancelCaptchaCallback( Tp::memFun(this, &YSConnection::cancelCaptcha));
        baseChannel->plugInterface(AbstractChannelInterfacePtr::dynamicCast(captchaIface));

        baseChannel->registerObject(error);
        if(!error->isValid())
            addChannel( baseChannel );
#else
        setStatus(ConnectionStatusDisconnected, ConnectionStatusReasonAuthenticationFailed);
#endif
    }
}

#ifdef USE_CAPTCHA_FOR_REGISTRATION
/* Authentication */
void YSConnection::getCaptchas(Tp::CaptchaInfoList& captchaInfoList, uint& numberRequired,
                               QString& language, Tp::DBusError* error)
{
    CaptchaInfo captchaInfo;
    captchaInfo.ID = 0;
    captchaInfo.type = "qa";
    captchaInfo.label = "";
    captchaInfo.flags = CaptchaFlagRequired;
    captchaInfo.availableMIMETypes = QStringList() << "text/plain";
    captchaInfoList << captchaInfo;

    numberRequired = 1;
    language = "en_US";
}

QByteArray YSConnection::getCaptchaData(uint ID, const QString& mimeType, Tp::DBusError* error)
{
    qDebug() << "YSConnection::getCaptchaData " << ID << " " << mimeType;
    QString countryCode;
    QString phonenumber;
    bool useText = true;
    GILStateHolder gstate;
    python::object ret = pythonInterface->call_intern("code_request", countryCode, phonenumber, mUID, useText);
    python::extract<QString> getStr(ret);
    if(!getStr.check()) {
        qDebug() << "YSConnection::getCaptchaData: return is not a string";
        error->set(TP_QT_ERROR_NOT_AVAILABLE,"Could not request code");
        return QByteArray();
    }
    if(getStr() != "send") {
        qDebug() << "YSConnection::getCaptchaData: status is not send: " << getStr();
        error->set(TP_QT_ERROR_NOT_AVAILABLE,"Could not request code");
        return QByteArray();
    }
    return QByteArray("Please enter the code you received via text");
}

void YSConnection::answerCaptchas(const Tp::CaptchaAnswers& answers, Tp::DBusError* error)
{
    qDebug() << "YSConnection::answerCaptchas " << answers;
    if(!answers.contains(0))
        return;
    QString code = answers[0];
    if(code.length() == 7) //remove the hyphon in the middle
        code = code.left(3) + code.right(3);

    if(!QRegExp("\\d{6}").exactMatch(code)) {
        qDebug() << "YSConnection::answerCaptchas: code does not have right form " << code;
    }

    QString countryCode;
    QString phonenumber;
    GILStateHolder gstate;
    python::object ret = pythonInterface->call_intern("code_register", countryCode, phonenumber, mUID, code);
    python::extract<QString> getStr(ret);
    if(!getStr.check()) {
        qDebug() << "YSConnection::answerCaptchas: return is not a string";
        error->set(TP_QT_ERROR_NOT_AVAILABLE,"Could not request code");
        return;
    }
    if(getStr() != "ok") {
        qDebug() << "YSConnection::answerCaptchas: status is not ok: " << getStr();
        captchaIface->setCaptchaError(TP_QT_ERROR_AUTHENTICATION_FAILED);
        captchaIface->setCaptchaStatus(CaptchaStatusTryAgain);
        return;
    }
    captchaIface->setCaptchaStatus(CaptchaStatusSucceeded);
    //FIXME: do something with the password

    //connect()
}

void YSConnection::cancelCaptcha(uint reason, const QString& debugMessage, Tp::DBusError* error)
{
    qDebug() << "YSConnection::cancelCaptcha " << reason << " " << debugMessage;
    captchaIface->setCaptchaStatus(CaptchaStatusFailed);
    setStatus(ConnectionStatusDisconnected, ConnectionStatusReasonAuthenticationFailed);
}
#endif

QStringList YSConnection::inspectHandles(uint handleType, const Tp::UIntList& handles, Tp::DBusError *error)
{
    qDebug() << "YSConnection::inspectHandles " << handles;
    QStringList ret;

    if( handleType == Tp::HandleTypeContact || handleType == HandleTypeRoom) {
        for( uint handle : handles ) {
            auto i = mHandles.left.find(handle);
            if( i == mHandles.left.end() ) {
                error->set(TP_QT_ERROR_INVALID_HANDLE,"Handle not found");
                return QStringList();
            }
            qDebug() << "inspectHandles " << handle << " = " << i->second;
            ret.append( i->second );
        }
        return ret;
    } else if(handleType == HandleTypeNone) {
        for( uint handle : handles ) {
            if(handle != 0) {
                error->set(TP_QT_ERROR_INVALID_HANDLE,"Handle not found");
                return QStringList();
            }
            ret.append( "" );
        }
        return ret;
    } else {
        qDebug() << "YSConnection::inspectHandles: unsupported handle type " << handleType;
        error->set(TP_QT_ERROR_INVALID_ARGUMENT,"Type unknown");
        return QStringList();
    }
}

Tp::ContactAttributesMap YSConnection::getContactAttributes(const Tp::UIntList& handles,
                                                            const QStringList& interfaces,
                                                            Tp::DBusError*)
{
    Tp::ContactAttributesMap ret;
    for( uint handle : handles ) {
        auto i = mHandles.left.find(handle);
        if( i == mHandles.left.end())
            continue;
        if( !isContactId(i->second) )
            continue;
        qDebug() << "getContactAttributes " << handle << " = " << i->second;
        QVariantMap attributes;
        //org.freedesktop.Telepathy.Connection.Interface.SimplePresence/presence
        attributes["org.freedesktop.Telepathy.Connection/contact-id"] = i->second;
        if(handle != selfHandle) {
            attributes["org.freedesktop.Telepathy.Connection.Interface.ContactList/subscribe"] = mContactsSubscription[handle];
            attributes["org.freedesktop.Telepathy.Connection.Interface.ContactList/publish"] = SubscriptionStateYes;
            attributes["org.freedesktop.Telepathy.Connection.Interface.SimplePresence/presence"] = QVariant::fromValue( getPresence(handle) );
        }
        ret[handle] = attributes;
    }
    qDebug() << "YSConnection::getContactAttributes " << handles
             << " = " << ret;
    return ret;

}

Tp::ContactAttributesMap YSConnection::getContactListAttributes(const QStringList& interfaces,
                                                                bool /*hold*/, Tp::DBusError* /*error*/)
{
    Tp::ContactAttributesMap contactAttributeMap;
    for( auto i : mHandles.left )
    {
        uint handle = i.first;
        if(handle == selfHandle)
            continue;
        if( !isContactId(i.second) )
            continue;
        QVariantMap attributes;
        //org.freedesktop.Telepathy.Connection.Interface.ContactList/subscribe
        attributes["org.freedesktop.Telepathy.Connection/contact-id"] = i.second;
        attributes["org.freedesktop.Telepathy.Connection.Interface.ContactList/subscribe"] = mContactsSubscription[handle];
        attributes["org.freedesktop.Telepathy.Connection.Interface.ContactList/publish"] = SubscriptionStateYes;
        attributes["org.freedesktop.Telepathy.Connection.Interface.SimplePresence/presence"] = QVariant::fromValue( getPresence(handle) );
        contactAttributeMap[handle] = attributes;
    }
    qDebug() << "YSConnection::getContactListAttributesCallback " << interfaces
             << " = " << contactAttributeMap;
    return contactAttributeMap;
}

void YSConnection::requestSubscription(const Tp::UIntList& contacts,
                                       const QString& message, Tp::DBusError* error)
{
    qDebug() << "YSConnection::requestSubscription " << contacts;
    for( uint handle : contacts ) {
        QString jid = getIdentifier(handle);
        if(jid.length() == 0) {
            error->set(TP_QT_ERROR_INVALID_HANDLE,"Handle not found");
            return;
        }
        if(!isContactId(jid)) {
            error->set(TP_QT_ERROR_INVALID_HANDLE,"Handle is not a ContactHandle");
            return;
        }
        pythonInterface->call("presence_subscribe", jid);
        setSubscriptionState(QStringList() << jid, QList<uint>() << handle, SubscriptionStateYes);
    }
}

Tp::UIntList YSConnection::requestHandles(uint handleType, const QStringList& identifiers, Tp::DBusError* error)
{
    Tp::UIntList ret;

    if( handleType != Tp::HandleTypeContact && handleType != Tp::HandleTypeRoom ) {
        qDebug() << "YSConnection::requestHandles: handleType " << handleType << " not supported for ids " << identifiers;
        error->set(TP_QT_ERROR_INVALID_ARGUMENT,"Type unknown");
        return ret;
    }

    for( const QString& identifier : identifiers ) {

        if(( handleType == Tp::HandleTypeContact && !isContactId(identifier) )
           ||( handleType == Tp::HandleTypeRoom && !isGroupId(identifier) )) {
            error->set(TP_QT_ERROR_INVALID_ARGUMENT,"Identifier not valid for that handleType");
            return ret;
        }

        auto i = mHandles.right.find(identifier);
        if( i == mHandles.right.end()) {
            if(handleType == Tp::HandleTypeContact && isValidContact(identifier)) {
                //Check if that identifier is at whatsapp
                ret.push_back(addContact(identifier));
            } else {
                qDebug() << "YSConnection::requestHandles: id invalid " << identifier;
                error->set(TP_QT_ERROR_INVALID_HANDLE,"Handle not found");
                return Tp::UIntList();
            }
        } else {
            ret.push_back(i->second);
        }
    }

    qDebug() << "YSConnection::requestHandles " << identifiers
             << " = " << ret;
    return ret;
}

Tp::BaseChannelPtr YSConnection::createChannel(const QString& channelType, uint targetHandleType,
                                               uint targetHandle, Tp::DBusError *error)
{
    qDebug() << "YSConnection::createChannel " << channelType
             << " " << targetHandleType
             << " " << targetHandle;
    Q_ASSERT(error);

    if(channelType == TP_QT_IFACE_CHANNEL_TYPE_ROOM_LIST) {

        BaseChannelPtr baseChannel = BaseChannel::create(this, channelType, targetHandle, targetHandleType);
        BaseChannelRoomListTypePtr roomListType = BaseChannelRoomListType::create("");
        roomListType->setListRoomsCallback( [this, roomListType] (Tp::DBusError* error) {
                                                        listRooms( roomListType, error) ;
                                             } );
        baseChannel->plugInterface(AbstractChannelInterfacePtr::dynamicCast(roomListType));
        return baseChannel;
    }

    if( (targetHandleType != Tp::HandleTypeContact && targetHandleType != Tp::HandleTypeRoom)
            || targetHandle == 0) {
        error->set(TP_QT_ERROR_INVALID_HANDLE,"Handle not found");
        return BaseChannelPtr();
    }

    auto i = mHandles.left.find(targetHandle);
    if( i == mHandles.left.end() ) {
        error->set(TP_QT_ERROR_INVALID_HANDLE,"Handle not found");
        return BaseChannelPtr();
    }
    QString id = i->second;

    if( targetHandleType != getType(id) ) {
        qDebug() << "Type mismatch " << targetHandleType << " " << getType(id);
        error->set(TP_QT_ERROR_INVALID_ARGUMENT,"handle not valid for that handleType");
        return BaseChannelPtr();
    }

    BaseChannelPtr baseChannel = BaseChannel::create(this, channelType, targetHandle, targetHandleType);

    if(channelType == TP_QT_IFACE_CHANNEL_TYPE_TEXT) {
        BaseChannelTextTypePtr textType = BaseChannelTextType::create(baseChannel.data());
        qDebug() << "Text interface is called " << textType->interfaceName();
        baseChannel->plugInterface(AbstractChannelInterfacePtr::dynamicCast(textType));


        QStringList supportedContentTypes = QStringList() << "text/plain";
        UIntList messageTypes = UIntList() << ChannelTextMessageTypeNormal << ChannelTextMessageTypeDeliveryReport;
        uint messagePartSupportFlags = 0;
        uint deliveryReportingSupport = DeliveryReportingSupportFlagReceiveSuccesses;
        BaseChannelMessagesInterfacePtr messagesIface = BaseChannelMessagesInterface::create(textType.data(),
                                                                                             supportedContentTypes,
                                                                                             messageTypes,
                                                                                             messagePartSupportFlags,
                                                                                             deliveryReportingSupport);
        messagesIface->setSendMessageCallback(
                    [this, id] (const Tp::MessagePartList& message, uint flags, Tp::DBusError* error) {
                        return sendMessage(id, message, flags, error );
                    });
        baseChannel->plugInterface(AbstractChannelInterfacePtr::dynamicCast(messagesIface));
    }

    if(targetHandleType == Tp::HandleTypeRoom) {
        ChannelGroupFlags flags;
        flags = flags | ChannelGroupFlagCanAdd | ChannelGroupFlagCanRemove | ChannelGroupFlagProperties;
        BaseChannelGroupInterfacePtr groupIface = BaseChannelGroupInterface::create(flags, selfHandle);
        //groupIface->setAddMembersCallback(); FIXME
        //groupIface->setRemoveMembersCallback();
        groupIface->addMembers(Tp::UIntList() << selfHandle,
                               QStringList() << getIdentifier(selfHandle));
        baseChannel->plugInterface( AbstractChannelInterfacePtr::dynamicCast(groupIface) );

        if(rooms.contains(targetHandle)) {
            const Room& room = rooms[targetHandle];
            BaseChannelRoomInterfacePtr roomIface = BaseChannelRoomInterface::create(room.subject,
                                                                                     "",
                                                                                     room.owner,
                                                                                     ensureHandle(room.owner),
                                                                                     room.creationTimestamp);
            baseChannel->plugInterface( AbstractChannelInterfacePtr::dynamicCast(roomIface) );

            BaseChannelSubjectInterfacePtr subjectIface = BaseChannelSubjectInterface::create();
            subjectIface->setSubject( room.subject );
            subjectIface->setActor( room.subjectOwner );
            subjectIface->setActorHandle( ensureContact(room.subjectOwner) );
            subjectIface->setTimestamp( room.subjectTimestamp );
            subjectIface->setCanSet( false );
            baseChannel->plugInterface( AbstractChannelInterfacePtr::dynamicCast(subjectIface) );
        } else {
            qWarning() << "createChannel: room info for handle does not exist: " << targetHandle;
        }
    }

    return baseChannel;
}

/* Called when a telepathy client has acknowledged receiving this message */
void YSConnection::messageAcknowledged(QString /*id*/) {
}

QString YSConnection::sendMessage(const QString& jid, const Tp::MessagePartList& message, uint /*flags*/,
                                  Tp::DBusError* error) {
    //We ignore flags and always post delivery reports
    qDebug() << "YSConnection::sendMessage ";

    QString content;
    for(MessagePartList::const_iterator i = message.begin()+1; i != message.end(); ++i)
        if(i->count(QLatin1String("content-type"))
            && i->value(QLatin1String("content-type")).variant().toString() == QLatin1String("text/plain")
            && i->count(QLatin1String("content")))
        {
            content = i->value(QLatin1String("content")).variant().toString();
            break;
        }

    GILStateHolder gstate;
    python::object pMsgId = pythonInterface->call("message_send", jid, content.toUtf8());
    python::extract<QString> get_msgId(pMsgId);
    if(!get_msgId.check()) {
        qDebug() << "message_send did not return a string";
        error->set(TP_QT_ERROR_INVALID_ARGUMENT,"Internal error");
        return "";
    }

    qDebug() << "YSConnection::sendMessage with id " << get_msgId();
    return get_msgId();
}

uint YSConnection::setPresence(const QString& status, const QString& message, Tp::DBusError* error)
{
    qDebug() << "YSConncetion::setPresence " << status << " " << message;
    return selfHandle;
}

/*
 * If field == 'tel', sync's the given contact's phonenumbers with whatsapp to see if they
 * are registered there.
 */
void YSConnection::getContactsByVCardField(const QString& field, const QStringList& addresses,
                                           const QStringList& interfaces,
                                           Tp::AddressingNormalizationMap& addressingNormalizationMap,
                                           Tp::ContactAttributesMap& contactAttributesMap, Tp::DBusError* error) {
    qDebug() << "YSConnection::getContactsByVCardField " << field << " " << addresses;

    if(field != "tel") {
        error->set(TP_QT_ERROR_INVALID_ARGUMENT,"Only field 'tel' is supported");
        return;
    }

    GILStateHolder gstate;
    QString numbers = addresses.join(",");
    python::object ret = pythonInterface->call_intern("syncContacts", mPhoneNumber, mPassword, numbers);
    python::extract<python::list> getList(ret);
    if(!getList.check()) {
        qDebug() << "YSConnection::getContactsByVCardField: return value is not a list";
        error->set(TP_QT_ERROR_NOT_AVAILABLE,"YSConnection::getContactsByVCardField: return value is not a list");
        return;
    }
    python::list l = getList();
        while(python::len(l)) {
        python::object entry = l.pop();
        python::extract<QString> getNumber(entry);
        if(!getNumber.check()) {
            qDebug() << "YSConnection::getContactsByVCardField: return value is not a QString";
            error->set(TP_QT_ERROR_NOT_AVAILABLE,"YSConnection::getContactsByVCardField: return value is not a QString");
            return;
        }
        qDebug() << "YSConnection::getContactsByVCardField: " << getNumber();
        QString identifier = getNumber() + "@s.whatsapp.net";
        addContact(identifier);
    }
}

void YSConnection::getContactsByURI(const QStringList& URIs, const QStringList& interfaces,
                     Tp::AddressingNormalizationMap& addressingNormalizationMap,
                     Tp::ContactAttributesMap& contactAttributesMap, Tp::DBusError* error) {
    qDebug() << "YSConnection::getContactsByURI " << URIs;
}

/*                                             YowsupInterface                                      */
void YSConnection::on_yowsup_auth_success(QString phonenumber) {
    qDebug() << "YSConnection::auth_success " << phonenumber;

    simplePresenceIface->setStatuses(Protocol::getSimpleStatusSpecMap());
    simplePresenceIface->setMaxmimumStatusMessageLength(20); //FIXME

    /* Set presence */
    SimpleContactPresences presences;
    SimplePresence presence;
    presence.status = "available";
    presence.statusMessage = "";
    presence.type = ConnectionPresenceTypeAvailable;
    presences[selfHandle] = presence;
    simplePresenceIface->setPresences(presences);

    /* Set connection status */
    setStatus(ConnectionStatusConnected, ConnectionStatusReasonRequested);

    /* Set ContactList status */
    contactListIface->setContactListState(ContactListStateSuccess);

    pythonInterface->runReaderThread();
    pythonInterface->call("presence_sendAvailable");
    pythonInterface->call("group_getGroups", QString(QLatin1String("participating")) ); //can also be "owning"
}

void YSConnection::on_yowsup_auth_fail(QString mobilenumber, QString reason) {
    qDebug() << "YSConnection::auth_fail for " << mobilenumber << " reason: " << reason;
    setStatus(ConnectionStatusDisconnected, ConnectionStatusReasonAuthenticationFailed);
}

void YSConnection::on_yowsup_status_dirty() {
    qDebug() << "YSConnection::status_dirty";
}

void YSConnection::on_yowsup_disconnected(QString reason) {
    qDebug() << "YSConnection::on_yowsup_disconnected: reason=" << reason;
    if(reason == "shutdown") //set in PythonInterface::~PythonInterface()
        setStatus(ConnectionStatusDisconnected, ConnectionStatusReasonRequested);
    else
        setStatus(ConnectionStatusDisconnected, ConnectionStatusReasonNetworkError);
}

void YSConnection::on_yowsup_receipt_messageSent(QString id,QString msgId) {

    uint handle = ensureHandle(id);
    bool yours;
    Tp::DBusError error;
    BaseChannelPtr channel = ensureChannel(TP_QT_IFACE_CHANNEL_TYPE_TEXT, getType(id), handle,
                                           yours, selfHandle, false,
                                           &error);
    if(error.isValid()) {
        qWarning() << "ensureChannel failed:" << error.name() << " " << error.message();
        return;
    }
    BaseChannelTextTypePtr textChannel = BaseChannelTextTypePtr::dynamicCast(channel->interface(TP_QT_IFACE_CHANNEL_TYPE_TEXT));

    if(!textChannel) {
        qDebug() << "Error, channel is not a textChannel??";
        return;
    }

    MessagePartList partList;
    MessagePart header;
    header["message-sender"]        = QDBusVariant(handle);
    header["message-sender-id"]     = QDBusVariant(id);
    header["message-type"]          = QDBusVariant(ChannelTextMessageTypeDeliveryReport);
    header["delivery-status"]       = QDBusVariant(DeliveryStatusAccepted);
    header["delivery-token"]        = QDBusVariant(msgId);
    partList << header;

    textChannel->addReceivedMessage(partList);
}

void YSConnection::on_yowsup_receipt_messageDelivered(QString id, QString msgId) {
    pythonInterface->call("delivered_ack", id, msgId);

    uint handle = ensureHandle(id);
    bool yours;
    Tp::DBusError error;
    BaseChannelPtr channel = ensureChannel(TP_QT_IFACE_CHANNEL_TYPE_TEXT, getType(id), handle,
                                           yours, selfHandle, false, &error);
    if(error.isValid()) {
        qWarning() << "ensureChannel failed:" << error.name() << " " << error.message();
        return;
    }
    BaseChannelTextTypePtr textChannel = BaseChannelTextTypePtr::dynamicCast(channel->interface(TP_QT_IFACE_CHANNEL_TYPE_TEXT));

    if(!textChannel) {
        qDebug() << "Error, channel is not a textChannel??";
        return;
    }
    MessagePartList partList;
    MessagePart header;
    header["message-sender"]        = QDBusVariant(handle);
    header["message-sender-id"]     = QDBusVariant(id);
    header["message-type"]          = QDBusVariant(ChannelTextMessageTypeDeliveryReport);
    header["delivery-status"]       = QDBusVariant(DeliveryStatusDelivered);
    header["delivery-token"]        = QDBusVariant(msgId);
    partList << header;

    textChannel->addReceivedMessage(partList);
}

void YSConnection::on_yowsup_presence_available(QString jid) {
    uint handle = ensureContact(jid);
    if(!handle) {
        qDebug() << "YSConnection::on_yowsup_presence_available: could not create contact " << jid;
        return;
    }
    setPresenceState(QList<uint>() << handle, "available");
    if(handle != selfHandle)
        setSubscriptionState(QStringList() << jid, QList<uint>() << handle, SubscriptionStateYes);
}

void YSConnection::on_yowsup_presence_unavailable(QString jid) {
    uint handle = ensureContact(jid);
    if(!handle) {
        qDebug() << "YSConnection::on_yowsup_presence_unavailable: could not create contact " << jid;
        return;
    }
    setPresenceState(QList<uint>() << handle, "offline");
    if(handle != selfHandle)
        setSubscriptionState(QStringList() << jid, QList<uint>() << handle, SubscriptionStateYes);
}

void YSConnection::yowsup_messageReceived(QString msgId, QString jid, const MessagePartList& body, uint timestamp,
                                          bool wantsReceipt, const QString& gid) {
    qDebug() << "YSConnection::yowsup_messageReceived " << msgId;
    //We cannot wait until messageAcknowledged(), because that indicates that the user saw the message,
    //not that it was received. Yowsup won't tolerate such long delays.
    if(wantsReceipt)
        pythonInterface->call("message_ack", gid.isEmpty() ? jid : gid, msgId );

    uint senderHandle, targetHandle;
    QString senderId, targetId;
    HandleType handleType;
    if(gid.isEmpty()) {
        senderHandle = targetHandle = ensureContact(jid);
        senderId = targetId = jid;
        handleType = HandleTypeContact;
    } else {
        senderHandle = ensureContact(jid);
        senderId = jid;
        targetHandle = ensureGroup(gid);
        targetId = gid;
        handleType = HandleTypeRoom;
    }
    //TODO: initiator should be group creator
    Tp::DBusError error;
    bool yours;
    BaseChannelPtr channel = ensureChannel(TP_QT_IFACE_CHANNEL_TYPE_TEXT, handleType, targetHandle, yours,
                                           senderHandle,
                                           false, &error);
    if(error.isValid()) {
        qWarning() << "ensureChannel failed:" << error.name() << " " << error.message();
        return;
    }

    if(handleType == HandleTypeRoom) {
        BaseChannelGroupInterfacePtr groupIface = BaseChannelGroupInterfacePtr::dynamicCast(channel->interface(TP_QT_IFACE_CHANNEL_INTERFACE_GROUP));
        Q_ASSERT(!groupIface.isNull());
        groupIface->addMembers(Tp::UIntList() << senderHandle << selfHandle,
                               QStringList() << senderId << getIdentifier(selfHandle));
    }

    BaseChannelTextTypePtr textChannel = BaseChannelTextTypePtr::dynamicCast(channel->interface(TP_QT_IFACE_CHANNEL_TYPE_TEXT));
    if(!textChannel) {
        qDebug() << "Error, channel is not a textChannel??";
        return;
    }
    if(timestamp == 0)
        timestamp = QDateTime::currentMSecsSinceEpoch()/1000;
    MessagePartList partList;
    MessagePart header;
    header["message-token"]         = QDBusVariant(msgId);
    header["message-received"]      = QDBusVariant(timestamp);
    header["message-sender"]        = QDBusVariant(senderHandle);
    header["message-sender-id"]     = QDBusVariant(senderId);
    //header["sender-nickname"]       = QDBusVariant(pushName);
    header["message-type"]          = QDBusVariant(ChannelTextMessageTypeNormal);

    partList << header << body;
    textChannel->addReceivedMessage(partList);
}

void YSConnection::on_yowsup_message_received(QString msgId, QString jid, QString content, uint timestamp,
                                    bool wantsReceipt, QString pushName, bool isBroadcast) {
    qDebug() << "YSConnection::message_received " << msgId <<  " " << jid << " " << content;

    MessagePart body;
    body["content-type"]            = QDBusVariant("text/plain");
    body["content"]                 = QDBusVariant(content);
    yowsup_messageReceived(msgId, jid, MessagePartList() << body, timestamp, wantsReceipt);
}


void YSConnection::on_yowsup_group_messageReceived(QString msgId,QString gid,QString jid,QString content,int timestamp, bool wantsReceipt, QString pushName) {
    MessagePart body;
    body["content-type"]            = QDBusVariant("text/plain");
    body["content"]                 = QDBusVariant(content);
    yowsup_messageReceived(msgId, jid, MessagePartList() << body, timestamp, wantsReceipt, gid);
}

void YSConnection::yowsup_linked_data_received(const char* type, QString msgId, QString jid, QString preview,
                                                  QString url, QString size, bool wantsReceipt, const QString& gid) {

    MessagePartList body;
    MessagePart text;
    text["content-type"]            = QDBusVariant("text/plain");
    text["content"]                 = QDBusVariant(QLatin1String(type) + ": " + url + " [" + formatSize(size) + "] ");
    text["x-whosthere-type"]        = QDBusVariant(type);
    text["x-whosthere-size"]        = QDBusVariant(size);
    text["x-whosthere-url"]         = QDBusVariant(url);
    body << text;
    if(preview.length() > 0) {
        MessagePart img;
        img["content-type"]         = QDBusVariant("image/jpeg");
        img["content"]              = QDBusVariant(QByteArray::fromBase64(preview.toLatin1()));
        img["thumbnail"]            = QDBusVariant(true);
        body << img;
    }
    yowsup_messageReceived(msgId, jid, body, 0, wantsReceipt, gid);
}

void YSConnection::on_yowsup_image_received(QString msgId, QString jid, QString preview,
                                            QString url, QString size, bool wantsReceipt, bool isBroadcast) {

    yowsup_linked_data_received("image", msgId, jid, preview, url, size, wantsReceipt);
}

void YSConnection::on_yowsup_group_imageReceived(QString msgId,QString gid,QString jid,QString preview,QString url,QString size,bool wantsReceipt){
    yowsup_linked_data_received("image", msgId, jid, preview, url, size, wantsReceipt, gid);
}

void YSConnection::on_yowsup_video_received(QString msgId, QString jid, QString preview, QString url, QString size, bool wantsReceipt, bool isBroadcast){

    yowsup_linked_data_received("video", msgId, jid, preview, url, size, wantsReceipt);
}

void YSConnection::on_yowsup_group_videoReceived(QString msgId,QString gid,QString jid,QString preview,QString url,QString size,bool wantsReceipt){
    yowsup_linked_data_received("video", msgId, jid, preview, url, size, wantsReceipt, gid);
}

void YSConnection::on_yowsup_audio_received(QString msgId,QString jid,QString url,QString size,bool wantsReceipt, bool isBroadcast){
    yowsup_linked_data_received("audio", msgId, jid, "", url, size, wantsReceipt);
}

void YSConnection::on_yowsup_group_audioReceived(QString msgId,QString gid,QString jid,QString url, QString size, bool wantsReceipt){
    yowsup_linked_data_received("audio", msgId, jid, "", url, size, wantsReceipt, gid);
}

void YSConnection::yowsup_location_received(QString msgId, QString jid,
                                            QString name, QString preview,
                                            QString latitude, QString longitude,
                                            bool wantsReceipt, QString gid) {

    QString content;
    QTextStream stream(&content);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    if(name.length() > 0)
        stream << "location: \"" << name  << "\" at https://maps.google.com/maps?q=" << latitude << "," << longitude;
    else
        stream << "location: https://maps.google.com/maps?q=" << latitude << "," << longitude;

    MessagePartList body;
    MessagePart text;
    text["content-type"]            = QDBusVariant("text/plain");
    text["content"]                 = QDBusVariant(content);
    text["x-whosthere-type"]        = QDBusVariant("location");
    text["x-whosthere-name"]        = QDBusVariant(name);
    text["x-whosthere-latitude"]    = QDBusVariant(latitude);
    text["x-whosthere-longitude"]   = QDBusVariant(longitude);
    body << text;
    MessagePart img;
    img["content-type"]         = QDBusVariant("image/jpeg");
    img["content"]              = QDBusVariant(QByteArray::fromBase64(preview.toLatin1()));
    img["thumbnail"]            = QDBusVariant(true);
    body << img;
    yowsup_messageReceived(msgId, jid, body, 0, wantsReceipt, gid);
}

void YSConnection::on_yowsup_location_received(QString msgId,QString jid,QString name,QString preview,QString latitude,QString longitude,bool wantsReceipt, bool isBroadcast){
    yowsup_location_received(msgId, jid, name, preview, latitude, longitude, wantsReceipt);
}

void YSConnection::on_yowsup_group_locationReceived(QString msgId,QString gid,QString jid,QString name, QString preview,QString latitude, QString longitude, bool wantsReceipt){
    yowsup_location_received(msgId, jid, name, preview, latitude, longitude, wantsReceipt, gid);
}

void YSConnection::yowsup_vcard_received(QString msgId,QString jid,QString name, QString data,bool wantsReceipt, QString gid) {

    MessagePartList body;
    MessagePart text;
    text["content-type"]            = QDBusVariant("text/plain");
    text["content"]                 = QDBusVariant(QLatin1String("vcard: ") + data);
    text["x-whosthere-type"]        = QDBusVariant("vcard");
    text["x-whosthere-name"]        = QDBusVariant(name);
    text["x-whosthere-vcard"]       = QDBusVariant(data);
    body << text;
    MessagePart vcard;
    vcard["content-type"]         = QDBusVariant("text/vcard");
    vcard["content"]              = QDBusVariant(data);
    body << vcard;
    yowsup_messageReceived(msgId, jid, body, 0, wantsReceipt, gid);
}

void YSConnection::on_yowsup_vcard_received(QString msgId,QString jid,QString name, QString data,bool wantsReceipt, bool isBroadcast){
    yowsup_vcard_received(msgId, jid, name, data, wantsReceipt);
}

void YSConnection::on_yowsup_group_vcardReceived(QString msgId,QString gid,QString jid,QString name,QString data,bool wantsReceipt){
    yowsup_vcard_received(msgId, jid, name, data, wantsReceipt, gid);
}

void YSConnection::on_yowsup_notification_contactProfilePictureUpdated(QString jid, uint timestamp,QString msgId,int pictureId, bool wantsReceipt){
    qDebug() << "YSConnection::on_yowsup_notification_contactProfilePictureUpdated";
    if(wantsReceipt)
        pythonInterface->call("notification_ack", jid, msgId );
}

void YSConnection::on_yowsup_notification_contactProfilePictureRemoved(QString jid, uint timestamp,QString msgId, bool wantsReceipt){
    qDebug() << "YSConnection::on_yowsup_notification_contactProfilePictureRemoved";
    if(wantsReceipt)
        pythonInterface->call("notification_ack", jid, msgId );
}

void YSConnection::on_yowsup_notification_groupParticipantAdded(QString gid, QString jid, QString author, uint timestamp,QString msgId, bool wantsReceipt){
    qDebug() << "YSConnection::on_yowsup_notification_groupParticipantAdded";
    if(wantsReceipt)
        pythonInterface->call("notification_ack", gid, msgId );
}

void YSConnection::on_yowsup_notification_groupParticipantRemoved(QString gid, QString jid, QString author, uint timestamp,QString msgId,bool wantsReceipt){
    qDebug() << "YSConnection::on_yowsup_notification_groupParticipantRemoved";
    if(wantsReceipt)
        pythonInterface->call("notification_ack", gid, msgId );
}

void YSConnection::on_yowsup_notification_groupPictureUpdated(QString gid, QString jid, uint timestamp, QString msgId, int pictureId, bool wantsReceipt){
    qDebug() << "YSConnection::on_yowsup_notification_groupPictureUpdated";
    if(wantsReceipt)
        pythonInterface->call("notification_ack", gid, msgId );
}

void YSConnection::on_yowsup_notification_groupPictureRemoved(QString gid, QString jid, uint timestamp, QString msgId, bool wantsReceipt){
    qDebug() << "YSConnection::on_yowsup_notification_groupPictureRemoved";
    if(wantsReceipt)
        pythonInterface->call("notification_ack", gid, msgId );
}

void YSConnection::on_yowsup_group_subjectReceived(QString msgId,QString gid,QString jid,QString newSubject,uint timestamp,bool wantsReceipt) {
    qDebug() << "YSConnection::on_yowsup_group_subjectReceived";
    if(wantsReceipt)
        pythonInterface->call("message_ack", gid, msgId );
}

void YSConnection::on_yowsup_profile_setStatusSuccess(QString jid, QString msgId) {
    qDebug() << "YSConnection::on_yowsup_profile_setStatusSuccess";
    pythonInterface->call("delivered_ack", jid, msgId );
}

/* Group listing */
void YSConnection::listRooms(BaseChannelRoomListTypePtr roomListType, Tp::DBusError* error) {
    qDebug() << "YSConnection::listRooms";
    Tp::RoomInfoList roomInfoList;

    for( Room& room : rooms ) {
        Tp::RoomInfo roomInfo;

        roomInfo.channelType = TP_QT_IFACE_CHANNEL_TYPE_TEXT;
        roomInfo.handle = getHandle(room.id);
        roomInfo.info["handle-name"] = room.id;
        roomInfo.info["name"] = room.subject;
        roomInfo.info["subject"] = room.subject;
        roomInfo.info["members"] = room.members.size();
        roomInfo.info["password"] = false;
        roomInfo.info["invite-only"] = false;

        roomInfoList << roomInfo;
    }
    roomListType->gotRooms(roomInfoList);
}

void YSConnection::on_yowsup_group_gotInfo(QString gid, QString jid,
                                           QString subject, QString subjectOwner,
                                           qlonglong subjectT, qlonglong creation) {
    qDebug() << "YSConnection::on_yowsup_group_gotInfo";
    Room room;
    room.id = gid;
    room.owner = jid;
    room.subject = subject;
    room.subjectOwner = subjectOwner;
    room.creationTimestamp = creation;
    room.subjectTimestamp = subjectT;
    //room.members
    uint roomHandle = ensureHandle(gid);
    rooms[roomHandle] = room;
}

/* Convenience */
bool YSConnection::isValidContact(const QString& identifier) {
    if(!isContactId(identifier))
        return false;

    GILStateHolder gstate;
    QString number = "+" + identifier.left(identifier.length()-QLatin1String("@s.whatsapp.net").size());
    python::object ret = pythonInterface->call_intern("syncContact", mPhoneNumber, mPassword, number);
    python::extract<int> getInt(ret);
    if(!getInt.check()) {
        qDebug() << "YSConnection::isValidContact: return value is a not a int";
        return false;
    }
    bool isValid = getInt();
    qDebug() << "YSConnection::isValidContact: " << isValid;
    return isValid;
}

bool YSConnection::isContactId(const QString& jid) {
    static QRegExp validId(R"(^\d+@s\.whatsapp\.net$)");
    return validId.exactMatch(jid);
}

bool YSConnection::isGroupId(const QString& gid) {
    static QRegExp validId(R"(^\d+-\d+@g\.us$)");
    return validId.exactMatch(gid);
}

QString YSConnection::getIdentifier(uint handle) {
    auto i = mHandles.left.find(handle);
    if( i == mHandles.left.end() )
        return "";
    else
        return i->second;
}

uint YSConnection::getHandle(const QString& id) {
    auto i = mHandles.right.find(id);
    if( i == mHandles.right.end() )
        return 0;
    else
        return i->second;
}

bool YSConnection::isValidHandle(uint handle) {
    return getIdentifier(handle).length() > 0;
}

HandleType YSConnection::getType(uint handle) {
    QString id = getIdentifier(handle);
    if(id.isEmpty())
        return HandleTypeNone;

    return getType(id);
}

HandleType YSConnection::getType(const QString& id) {
    if(isContactId(id))
        return HandleTypeContact;
    else if(isGroupId(id))
        return HandleTypeRoom;
    else
        return HandleTypeNone;
}

uint YSConnection::ensureHandle(QString id) {
    if(isContactId(id))
        return ensureContact(id);
    else if(isGroupId(id))
        return ensureGroup(id);
    else {
        qWarning() << "YSConnection::ensureHandle: invalid id " << id;
        return 0;
    }
}

uint YSConnection::ensureContact(QString jid) {
    Q_ASSERT(isContactId(jid));
    uint handle = getHandle(jid);
    if(!handle)
        handle = addContact(jid);
    return handle;
}

uint YSConnection::ensureGroup(QString gid) {
    Q_ASSERT(isGroupId(gid));
    uint handle = getHandle(gid);
    if(!handle)
        handle = addGroup(gid);
    return handle;
}

uint YSConnection::addGroup(const QString& gid) {
    uint handle = 0;
    for (auto i : mHandles.left)
        if( handle < i.first )
            handle = i.first;

    handle++; // id = maximum + 1, never 0
    mHandles.left.insert( make_pair(handle, gid) );

    return handle;
}

uint YSConnection::addContacts(const QStringList& jids) {
    uint handle = 0;
    for (auto i : mHandles.left)
        if( handle < i.first )
            handle = i.first;

    QList<uint> newHandles;
    foreach(const QString& jid, jids) {
        handle++; // id = maximum + 1, never 0
        mHandles.left.insert( make_pair(handle, jid) );
        newHandles << handle;
    }

    setPresenceState(newHandles, "unknown");
    setSubscriptionState(jids, newHandles, SubscriptionStateUnknown);

    return handle;
}

uint YSConnection::addContact(const QString &jid) {

    return addContacts(QStringList() << jid);
}

void YSConnection::setSubscriptionState(const QStringList& jids, const QList<uint> handles, uint state) {

    if(contactListIface.isNull())
        return;

    Tp::ContactSubscriptionMap changes;
    Tp::HandleIdentifierMap identifiers;

    for(int i = 0; i < jids.size(); ++i) {
        /* Send ContactList change signal */
        if(handles[i] == 1) /*selfHandle is not set yet */
            continue;
        Tp::ContactSubscriptions change;
        change.publish = SubscriptionStateYes;
        change.publishRequest = "";
        change.subscribe = state;
        changes[handles[i]] = change;
        identifiers[handles[i]] = jids[i];
        mContactsSubscription[handles[i]] = state;
    }
    Tp::HandleIdentifierMap removals;
    contactListIface->contactsChangedWithID(changes, identifiers, removals);
}

Tp::SimplePresence YSConnection::getPresence(uint handle) {
    if( mPresences.contains(handle) )
        return mPresences[handle];
    else {
        qWarning() << "YSConnection::getPresence: no presence for " << handle;
        return Tp::SimplePresence();
    }
}

void YSConnection::setPresenceState(const QList<uint> handles, const QString& status) {
    if(simplePresenceIface.isNull())
        return;

    Tp::SimpleContactPresences newPresences;
    SimpleStatusSpecMap statusSpecMap = Protocol::getSimpleStatusSpecMap();
    foreach( uint handle, handles) {
        auto i = statusSpecMap.find(status);
        if(i == statusSpecMap.end()) {
            qDebug() << "YSConnection::setPresenceState: status not found: " << status;
            return;
        }
        SimplePresence presence;
        presence.status = status;
        presence.statusMessage = ""; //FIXME
        presence.type = i->type;
        mPresences[handle] = presence;
        newPresences[handle] = presence;

    }
    simplePresenceIface->setPresences(newPresences);
}

QString YSConnection::generateUID()
{
    QString randomHex;

    for(int i = 0; i < 32; i++) {
    int n = qrand() % 16;
        randomHex.append(QString::number(n,16));
    }
    return randomHex;
}

QString YSConnection::formatSize(QString size_) {
    uint size = size_.toInt();
    QString ret;
    QTextStream stream(&ret);
    stream.setRealNumberPrecision(1);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    if(size > 1024*1024)
        stream << (size/1024.0/1024.0) << " MB";
    else if(size > 103)
        stream << (size/1024.0) << " KB";
    else
        stream << size << " B";
    return ret;
}
