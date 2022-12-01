#ifndef VFSEVENT_H
#define VFSEVENT_H

#include <QObject>

#include <QString>

class VfsEvent
{
public:
    explicit VfsEvent();

    unsigned char act;
    QString src;
    unsigned int _cookie;
    unsigned short major;
    unsigned char minor;
};

#endif // VFSEVENT_H
