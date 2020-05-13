#ifndef DEVICE_H
#define DEVICE_H
#include "deviceinfo.h"
#include "partitionvector.h"
#include <QtDBus/QtDBus>

namespace DiskManager {

class Device
{
public:
    Device();
    void enable_partition_naming(int length);    // > 0 => enable partition naming support
    bool partition_naming_supported() const;
    int get_max_partition_name_length() const;
    DeviceInfo getDeviceInfo();

public:
    Sector length;
    Sector heads ;
    QString m_path;
    Sector sectors ;
    Sector cylinders ;
    Sector cylsize ;
    QString model;
    QString serial_number;
    QString disktype;
    int sector_size ;
    int max_prims ;
    int highest_busy ;
    bool readonly ;
    PartitionVector partitions;
private:
    int max_partition_name_length;


};

}//namespace DiskManager
#endif // DEVICE_H
