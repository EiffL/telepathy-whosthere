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

#include "protocol.h"
#include "connection.h"

#include <TelepathyQt/BaseConnection>
#include <TelepathyQt/Constants>
#include <TelepathyQt/RequestableChannelClassSpec>
#include <TelepathyQt/RequestableChannelClassSpecList>
#include <TelepathyQt/Types>

#include <QLatin1String>
#include <QVariantMap>

using namespace Tp;

Protocol::Protocol(const QDBusConnection &dbusConnection, const QString &name)
    : BaseProtocol(dbusConnection, name)
{
    setParameters(ProtocolParameterList()
        << ProtocolParameter(QLatin1String("phonenumber"),
                             QLatin1String("s"), ConnMgrParamFlagRequired)
        << ProtocolParameter(QLatin1String("password"),
                             QLatin1String("s"), ConnMgrParamFlagRequired));

    setRequestableChannelClasses(
            RequestableChannelClassSpecList() << RequestableChannelClassSpec::textChat());

    setEnglishName(QLatin1String("WhatsApp"));
    setIconName(QLatin1String("example-icon"));
    setVCardField(QLatin1String("x-example"));

    // callbacks
    setCreateConnectionCallback(memFun(this, &Protocol::createConnection));
    setIdentifyAccountCallback(memFun(this, &Protocol::identifyAccount));
    setNormalizeContactCallback(memFun(this, &Protocol::normalizeContact));

    // Adressing
    addrIface = BaseProtocolAddressingInterface::create();
    addrIface->setAddressableVCardFields(QStringList() << QLatin1String("x-example-vcard-field"));
    addrIface->setAddressableUriSchemes(QStringList() << QLatin1String("example-uri-scheme"));
    addrIface->setNormalizeVCardAddressCallback(memFun(this, &Protocol::normalizeVCardAddress));
    addrIface->setNormalizeContactUriCallback(memFun(this, &Protocol::normalizeContactUri));
    plugInterface(AbstractProtocolInterfacePtr::dynamicCast(addrIface));

    // Avatars
    avatarsIface = BaseProtocolAvatarsInterface::create();
    avatarsIface->setAvatarDetails(AvatarSpec(QStringList() << QLatin1String("image/png"),
                16, 64, 32, 16, 64, 32, 1024));
    plugInterface(AbstractProtocolInterfacePtr::dynamicCast(avatarsIface));

    //Presence
    SimpleStatusSpec spAvailable;
    spAvailable.type = ConnectionPresenceTypeAvailable;
    spAvailable.maySetOnSelf = true;
    spAvailable.canHaveMessage = true;

    SimpleStatusSpec spOffline;
    spOffline.type = ConnectionPresenceTypeOffline;
    spOffline.maySetOnSelf = true;
    spOffline.canHaveMessage = false;

    SimpleStatusSpecMap specs;
    specs.insert(QLatin1String("available"), spAvailable);
    specs.insert(QLatin1String("offline"), spOffline);


    presenceIface = BaseProtocolPresenceInterface::create();
    presenceIface->setStatuses(PresenceSpecList(specs));
    plugInterface(AbstractProtocolInterfacePtr::dynamicCast(presenceIface));
}

Protocol::~Protocol()
{
}

static BaseConnectionPtr connection;
BaseConnectionPtr Protocol::createConnection(const QVariantMap &parameters, Tp::DBusError *error)
{
    qDebug() << "Protocol::createConnection " << name();
    if(connection.isNull()) {
        connection = BaseConnection::create<YSConnection>( "whosthere", name().toLatin1(), parameters);
        return connection;
    } else {
        error->set(QLatin1String("NormalizeVCardAddress.Error.Test"), QLatin1String("Only one connection allowed"));
        return BaseConnectionPtr();
    }
}

QString Protocol::identifyAccount(const QVariantMap &parameters, Tp::DBusError *error)
{
    qDebug() << "Protocol::identifyAccount";
    error->set(QLatin1String("IdentifyAccount.Error.Test"), QLatin1String(""));
    return QString();
}

QString Protocol::normalizeContact(const QString &contactId, Tp::DBusError *error)
{
    qDebug() << "Protocol::normalizeContact";
    error->set(QLatin1String("NormalizeContact.Error.Test"), QLatin1String(""));
    return QString();
}

QString Protocol::normalizeVCardAddress(const QString &vcardField, const QString vcardAddress,
        Tp::DBusError *error)
{
    qDebug() << "Protocol::normalizeVCardAddress";
    error->set(QLatin1String("NormalizeVCardAddress.Error.Test"), QLatin1String(""));
    return QString();
}

QString Protocol::normalizeContactUri(const QString &uri, Tp::DBusError *error)
{
    qDebug() << "Protocol::normalizeContactUri";
    error->set(QLatin1String("NormalizeContactUri.Error.Test"), QLatin1String(""));
    return QString();
}
