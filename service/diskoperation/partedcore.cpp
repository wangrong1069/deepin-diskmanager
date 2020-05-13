#include "partedcore.h"
#include "fsinfo.h"
#include "mountinfo.h"
#include "partition.h"
#include "procpartitionsinfo.h"
#include "filesystems/filesystem.h"
#include <unistd.h>
#include <fcntl.h>
#include <QDebug>

namespace DiskManager {
//hdparm可检测，显示与设定IDE或SCSI硬盘的参数。
//udevadm可检测设备热插拔
static bool udevadm_found = false;
static bool hdparm_found = false;
const std::time_t SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS = 1;
const std::time_t SETTLE_DEVICE_APPLY_MAX_WAIT_SECONDS = 10;
SupportedFileSystems *PartedCore::supported_filesystems = nullptr;

PartedCore::PartedCore(QObject *parent) : QObject(parent)
{
    for (PedPartitionFlag flag = ped_partition_flag_next(static_cast<PedPartitionFlag>(NULL)) ;
            flag ; flag = ped_partition_flag_next(flag))
        flags .push_back(flag) ;
    find_supported_core();
    supported_filesystems = new SupportedFileSystems();
    //Determine file system support capabilities for the first time
    supported_filesystems->find_supported_filesystems();
    probedeviceinfo();
}

void PartedCore::find_supported_core()
{
    udevadm_found = ! Utils::find_program_in_path("udevadm").isEmpty();
    hdparm_found = ! Utils::find_program_in_path("hdparm").isEmpty();
}

bool PartedCore::supported_filesystem(FSType fstype)
{
    return supported_filesystems->get_fs_object(fstype) != NULL;
}

const FS &PartedCore::get_fs(FSType fstype) const
{
    return supported_filesystems->get_fs_support(fstype);
}

FileSystem *PartedCore::get_filesystem_object(FSType fstype)
{
    return supported_filesystems->get_fs_object(fstype);
}

bool PartedCore::filesystem_resize_disallowed(const Partition &partition)
{
//    if (partition.fstype == FS_LVM2_PV) {
//        //The LVM2 PV can't be resized when it's a member of an export VG
//        QString vgname = LVM2_PV_Info::get_vg_name(partition.get_path());
//        if (vgname .isEmpty())
//            return false ;
//        return LVM2_PV_Info::is_vg_exported(vgname);
//    }
    return false ;
}

void PartedCore::insert_unallocated(const QString &device_path, PartitionVector &partitions, Sector start, Sector end, Byte_Value sector_size, bool inside_extended)
{
    //if there are no partitions at all..
    if (partitions .empty()) {
        Partition *partition_temp = new Partition();
        partition_temp->Set_Unallocated(device_path, start, end, sector_size, inside_extended);
        partitions.push_back_adopt(partition_temp);
        return ;
    }

    //start <---> first partition start
    if ((partitions .front() .sector_start - start) > (MEBIBYTE / sector_size)) {
        Sector temp_end = partitions.front().sector_start - 1;
        Partition *partition_temp = new Partition();
        partition_temp->Set_Unallocated(device_path, start, temp_end, sector_size, inside_extended);
        partitions.insert_adopt(partitions.begin(), partition_temp);
    }

    //look for gaps in between
    for (unsigned int t = 0 ; t < partitions .size() - 1 ; t++) {
        if (((partitions[ t + 1 ] .sector_start - partitions[ t ] .sector_end - 1) > (MEBIBYTE / sector_size))
                || ((partitions[ t + 1 ] .type != TYPE_LOGICAL)       // Only show exactly 1 MiB if following partition is not logical.
                    && ((partitions[ t + 1 ] .sector_start - partitions[ t ] .sector_end - 1) == (MEBIBYTE / sector_size))
                   )
           ) {
            Sector temp_start = partitions[t].sector_end + 1;
            Sector temp_end   = partitions[t + 1].sector_start - 1;
            Partition *partition_temp = new Partition();
            partition_temp->Set_Unallocated(device_path, temp_start, temp_end,
                                            sector_size, inside_extended);
            partitions.insert_adopt(partitions.begin() + ++t, partition_temp);
        }
    }

    //last partition end <---> end
    if ((end - partitions .back() .sector_end) >= (MEBIBYTE / sector_size)) {
        Sector temp_start = partitions.back().sector_end + 1;
        Partition *partition_temp = new Partition();
        partition_temp->Set_Unallocated(device_path, temp_start, end, sector_size, inside_extended);
        partitions.push_back_adopt(partition_temp);
    }
}

void PartedCore::set_flags(Partition &partition, PedPartition *lp_partition)
{
    for (unsigned int t = 0 ; t < flags .size() ; t++) {
        if (ped_partition_is_flag_available(lp_partition, flags[ t ]) && ped_partition_get_flag(lp_partition, flags[ t ]))
            partition .flags .push_back(ped_partition_flag_get_name(flags[ t ])) ;
    }
}

void PartedCore::probedeviceinfo(const QString &path)
{
    m_devicepaths.clear();
    BlockSpecial::clear_cache();
    ProcPartitionsInfo::load_cache();
    FsInfo::load_cache();
    MountInfo::load_cache();
    ped_device_probe_all();

    PedDevice *lp_device = ped_device_get_next(NULL) ;
    while (lp_device) {
        /* TO TRANSLATORS: looks like   Confirming /dev/sda */
        qDebug() << QString("Confirming %1").arg(lp_device->path);

        //only add this device if we can read the first sector (which means it's a real device)
        if (useable_device(lp_device))
            m_devicepaths.push_back(lp_device->path);
        qDebug() << lp_device->path;
        lp_device = ped_device_get_next(lp_device) ;
    }
    qDebug() << __FUNCTION__ << "devicepaths size=" << m_devicepaths.size();
    std::sort(m_devicepaths .begin(), m_devicepaths .end()) ;

    for (unsigned int t = 0 ; t < m_devicepaths .size() ; t++) {
        /*TO TRANSLATORS: looks like Searching /dev/sda partitions */
        Device temp_device;
        set_device_from_disk(temp_device, m_devicepaths[t]);
        //devices.push_back(temp_device);
        devicemap.insert(m_devicepaths.at(t), temp_device);
    }

    for (auto it = devicemap.begin(); it != devicemap.end(); it++) {
        DeviceInfo devinfo = it.value().getDeviceInfo();
        for (int i = 0; i < it.value().partitions.size(); i++) {
            Partition   pat = it->partitions[i];
            PartitionInfo partinfo = pat.getPartitionInfo();

            if (pat.type == TYPE_EXTENDED) {
                for (int k = 0; k < pat.logicals.size(); k++) {
                    Partition   plogic = pat.logicals[k];
                    partinfo = plogic.getPartitionInfo();
                    devinfo.partition.push_back(partinfo);
                }
            } else {
                devinfo.partition.push_back(partinfo);
            }
        }
        inforesult.insert(devinfo.m_path, devinfo);
    }
}

bool PartedCore::useable_device(const PedDevice *lp_device)
{
    Q_ASSERT(nullptr != lp_device);
    char *buf = static_cast<char *>(malloc(lp_device->sector_size));
    if (! buf)
        return false;

    // Must be able to read from the first sector before the disk device is considered
    // useable in GParted.
    bool success = false;
    int fd = open(lp_device->path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        ssize_t bytes_read = read(fd, buf, lp_device->sector_size);
        success = (bytes_read == lp_device->sector_size);
        close(fd);
    }
    free(buf);
    return success;
}

void PartedCore::set_device_from_disk(Device &device, const QString &device_path)
{
    PedDevice *lp_device = NULL;
    PedDisk *lp_disk = NULL;
    if (get_device(device_path, lp_device, true)) {
        device.heads       = lp_device->bios_geom.heads;
        device.length      = lp_device->length;
        device.m_path      = device_path;
        device.model       = lp_device->model;
        device.sector_size = lp_device->sector_size;
        device.sectors     = lp_device->bios_geom.sectors;
        device.cylinders   = lp_device->bios_geom.cylinders;
        device.cylsize     = device.heads * device.sectors;
        set_device_serial_number(device);
        if (device.cylsize < (MEBIBYTE / device.sector_size))
            device.cylsize = MEBIBYTE / device.sector_size;

        FSType fstype = detect_filesystem(lp_device, NULL);
        if (fstype != FS_UNKNOWN) {
            device.disktype = "none";
            device.max_prims = 1;
            set_device_one_partition(device, lp_device, fstype);
        } else if (get_disk(lp_device, lp_disk, false)) {

            // Partitioned drive (excluding "loop"), as recognised by libparted
            if (lp_disk && lp_disk->type && lp_disk->type->name &&
                    strcmp(lp_disk->type->name, "loop") != 0) {
                device.disktype = lp_disk->type->name;
                device.max_prims = ped_disk_get_max_primary_partition_count(lp_disk);

                // Determine if partition naming is supported.
                if (ped_disk_type_check_feature(lp_disk->type, PED_DISK_TYPE_PARTITION_NAME)) {
                    device.enable_partition_naming(
                        Utils::get_max_partition_name_length(device.disktype));
                }

                set_device_partitions(device, lp_device, lp_disk);

                if (device.highest_busy) {
                    device.readonly = ! commit_to_os(lp_disk, SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS);
                }
            }
            // Drive just containing libparted "loop" signature and nothing
            // else.  (Actually any drive reported by libparted as "loop" but
            // not recognised by blkid on the whole disk device).
            else if (lp_disk && lp_disk->type && lp_disk->type->name &&
                     strcmp(lp_disk->type->name, "loop") == 0) {
                device.disktype = lp_disk->type->name;
                device.max_prims = 1;

                // Create virtual partition covering the whole disk device
                // with unknown contents.
                Partition *partition_temp = new Partition();
                partition_temp->set_unpartitioned(device.m_path,
                                                  lp_device->path,
                                                  FS_UNKNOWN,
                                                  device.length,
                                                  device.sector_size,
                                                  false);
                // Place unknown file system message in this partition.
                device.partitions.push_back_adopt(partition_temp);
            }
            // Unrecognised, unpartitioned drive.
            else {
                device.disktype = "unrecognized";
                device.max_prims = 1;

                Partition *partition_temp = new Partition();
                partition_temp->set_unpartitioned(device.m_path,
                                                  "",  // Overridden with "unallocated"
                                                  FS_UNALLOCATED,
                                                  device.length,
                                                  device.sector_size,
                                                  false);
                device.partitions.push_back_adopt(partition_temp);
            }
        }
        destroy_device_and_disk(lp_device, lp_disk);
    }
}


bool PartedCore::get_device(const QString &device_path, PedDevice *&lp_device, bool flush)
{
    lp_device = ped_device_get(device_path.toStdString().c_str());
    if (lp_device) {
        if (flush)
            // Force cache coherency before going on to read the partition
            // table so that libparted reading the whole disk device and the
            // file system tools reading the partition devices read the same
            // data.
            flush_device(lp_device);
        return true;
    }
    return false;
}

bool PartedCore::get_disk(PedDevice *&lp_device, PedDisk *&lp_disk, bool strict)
{
    if (lp_device) {
        lp_disk = ped_disk_new(lp_device);

        // (#762941)(!46) After ped_disk_new() wait for triggered udev rules to
        // to complete which remove and re-add all the partition specific /dev
        // entries to avoid FS specific commands failing because they happen to
        // be running when the needed /dev/PTN entries don't exist.
        settle_device(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS);

        // if ! disk and writable it's probably a HD without disklabel.
        // We return true here and deal with them in
        // GParted_Core::set_device_from_disk().
        if (lp_disk || (! strict && ! lp_device ->read_only))
            return true;

        destroy_device_and_disk(lp_device, lp_disk);
    }

    return false;
}

void PartedCore::destroy_device_and_disk(PedDevice *&lp_device, PedDisk *&lp_disk)
{
    if (lp_disk)
        ped_disk_destroy(lp_disk) ;
    lp_disk = NULL ;

    if (lp_device)
        ped_device_destroy(lp_device) ;
    lp_device = NULL ;
}

void PartedCore::set_device_serial_number(Device &device)
{
    if (! hdparm_found)
        // Serial number left blank when the hdparm command is not installed.
        return;

    QString output, error;
    Utils::executcmd(QString("hdparm -I %1").arg(device.m_path), output, error);
    if (! error.isEmpty()) {
        // hdparm reported an error message to stderr.  Assume it's a device
        // without a hard drive serial number.
        //
        // Using hdparm -I to query Linux software RAID arrays and BIOS fake RAID
        // arrays, both devices without their own hard drive serial numbers,
        // produce this error:
        //     HDIO_DRIVE_CMD(identify) failed: Inappropriate ioctl for device
        //
        // And querying USB flash drives, also a device type without their own
        // hard drive serial numbers, generates this error:
        //     SG_IO: bad/missing sense data, sb[]:  70 00 05 00 00 00 00 0a ...
        device.serial_number = "none";
    } else {
        QString serial_number = Utils::regexp_label(output, "^[[:blank:]]*Serial Number:[[:blank:]]*(.*)[[:blank:]]*$");
        if (! serial_number.isEmpty())
            device.serial_number = serial_number;
    }
    // Otherwise serial number left blank when not found in the hdparm output.
}

void PartedCore::set_device_one_partition(Device &device, PedDevice *lp_device, FSType fstype)
{
    device.partitions.clear();
    QString path(lp_device->path);
    bool partition_is_busy = is_busy(fstype, path);

    Partition *partition_temp = NULL;
    if (fstype == FS_LUKS)
        partition_temp ;//= new PartitionLUKS();
    else
        partition_temp = new Partition();
    if (NULL == partition_temp)
        return;
    partition_temp->set_unpartitioned(device.m_path,
                                      path,
                                      fstype,
                                      device.length,
                                      device.sector_size,
                                      partition_is_busy);

//        if ( fstype == FS_LUKS )
//            set_luks_partition( *dynamic_cast<PartitionLUKS *>( partition_temp ) );

    if (partition_temp->busy)
        device.highest_busy = 1;

    set_partition_label_and_uuid(*partition_temp);
    set_mountpoints(*partition_temp);
    set_used_sectors(*partition_temp, NULL);
    device.partitions.push_back_adopt(partition_temp);
}

void PartedCore::set_partition_label_and_uuid(Partition &partition)
{
    QString partition_path = partition.get_path();
    // For SWRaid members only get the label and UUID from SWRaid_Info.  Never use
    // values from FS_Info to avoid showing incorrect information in cases where blkid
    // reports the wrong values.
//    if (partition.fstype == FS_LINUX_SWRAID ||
//            partition.fstype == FS_ATARAID) {
//        QString label = SWRaid_Info::get_label(partition_path);
//        if (! label.isEmpty())b
//            partition.set_filesystem_label(label);

//        partition.uuid = SWRaid_Info::get_uuid(partition_path);
//        return;
//    }

    // Retrieve file system label.  Use file system specific method first because it
    // has been that way since (#662537) to display Unicode labels correctly.
    // (#786502) adds support for reading Unicode labels through the FS_Info cache.
    // Either method should produce the same labels however the FS specific command is
    // run in the users locale where as blkid is run in the C locale.  Shouldn't
    // matter but who knows for sure!
    read_label(partition);
    if (! partition.filesystem_label_known()) {
        bool label_found = false;
        QString label = FsInfo::get_label(partition_path, label_found);
        if (label_found)
            partition.set_filesystem_label(label);
    }

    // Retrieve file system UUID.  Use cached method first in an effort to speed up
    // device scanning.
    partition.uuid = FsInfo::get_uuid(partition_path);
    if (partition.uuid.isEmpty()) {
        read_uuid(partition);
    }
}

bool PartedCore::is_busy(FSType fstype, const QString &path)
{
    FileSystem *p_filesystem = NULL ;
    bool busy = false ;

    if (supported_filesystem(fstype)) {
        switch (get_fs(fstype) .busy) {
        case FS::GPARTED:
            //Search GParted internal mounted partitions map
            busy = MountInfo::is_dev_mounted(path);
            break ;
        case FS::EXTERNAL:
            //Call file system specific method
            p_filesystem = get_filesystem_object(fstype) ;
            if (p_filesystem)
                busy = p_filesystem -> is_busy(path) ;
            break;

        default:
            break ;
        }
    } else {
//            DMRaid dmraid;

//            //Still search GParted internal mounted partitions map in case an
//            //  unknown file system is mounted
//            busy = Mount_Info::is_dev_mounted( path );

//            // Custom checks for recognised but other not-supported file system types.
//            busy |= (fstype == FS_LINUX_SWRAID && SWRaid_Info::is_member_active(path));
//            busy |= (fstype == FS_ATARAID      && (SWRaid_Info::is_member_active(path) ||
//                                                   dmraid.is_member_active(path)         ));
    }

    return busy ;
}

void PartedCore::read_label(Partition &partition)
{
    FileSystem *p_filesystem = NULL;
    switch (get_fs(partition.fstype).read_label) {
    case FS::EXTERNAL:
        p_filesystem = get_filesystem_object(partition.fstype);
        if (p_filesystem)
            p_filesystem->read_label(partition);
        break;

    default:
        break;
    }
}

void PartedCore::read_uuid(Partition &partition)
{
    FileSystem *p_filesystem = NULL;
    switch (get_fs(partition.fstype).read_uuid) {
    case FS::EXTERNAL:
        p_filesystem = get_filesystem_object(partition.fstype);
        if (p_filesystem)
            p_filesystem->read_uuid(partition);
        break;

    default:
        break;
    }
}

void PartedCore::set_mountpoints(Partition &partition)
{
    //DMRaid dmraid ; //Use cache of dmraid device information
    if (partition.fstype == FS_LVM2_PV) {
//        QString vgname = LVM2_PV_Info::get_vg_name(partition.get_path());
//        if (! vgname.isEmpty())
//            partition.add_mountpoint(vgname);
    } else if (partition.fstype == FS_LINUX_SWRAID) {
//        QString array_path = SWRaid_Info::get_array(partition.get_path());
//        if (! array_path.isEmpty())
//            partition.add_mountpoint(array_path);
    } else if (partition.fstype == FS_ATARAID) {
//        QString array_path = SWRaid_Info::get_array(partition.get_path());
//        if (! array_path.isEmpty()) {
//            partition.add_mountpoint(array_path);
//        } else {
//            array_path = dmraid.get_array(partition.get_path());
//            if (! array_path.isEmpty())
//                partition.add_mountpoint(array_path);
//        }
    } else if (partition.fstype == FS_LUKS) {
//        LUKS_Mapping mapping = LUKS_Info::get_cache_entry(partition.get_path());
//        if (! mapping.name.isEmpty())
//            partition.add_mountpoint(DEV_MAPPER_PATH + mapping.name);
    }
    // Swap spaces don't have mount points so don't bother trying to add them.
    else if (partition.fstype != FS_LINUX_SWAP) {
//        if (partition.busy) {
//#ifndef USE_LIBPARTED_DMRAID
//            // Handle dmraid devices differently because there may be more
//            // than one partition name.
//            // E.g., there might be names with and/or without a 'p' between
//            //       the device name and partition number.
//            if (dmraid.is_dmraid_device(partition.device_path)) {
//                // Try device_name + partition_number
//                QString dmraid_path = partition.device_path +
//                                            Utils::num_to_str(partition.partition_number);
//                if (set_mountpoints_helper(partition, dmraid_path))
//                    return;

//                // Try device_name + p + partition_number
//                dmraid_path = partition.device_path + "p" +
//                              Utils::num_to_str(partition.partition_number);
//                if (set_mountpoints_helper(partition, dmraid_path))
//                    return;
//            } else
//#endif
//            {
//                // Normal device, not DMRaid device
//                if (set_mountpoints_helper(partition, partition.get_path()))
//                    return;
//            }

//            if (partition.get_mountpoints().empty())
//                partition.push_back_message(_("Unable to find mount point"));
//        } else { // Not busy file system
//            partition.add_mountpoints(Mount_Info::get_fstab_mountpoints(partition.get_path()));
//        }
    }
}

void PartedCore::set_used_sectors(Partition &partition, PedDisk *lp_disk)
{
    if (supported_filesystem(partition.fstype)) {
        FileSystem *p_filesystem = NULL;
        if (partition.busy) {
            switch (get_fs(partition.fstype).online_read) {
            case FS::EXTERNAL:
                p_filesystem = get_filesystem_object(partition.fstype);
                if (p_filesystem)
                    p_filesystem->set_used_sectors(partition);
                break;
            case FS::GPARTED:
                mounted_fs_set_used_sectors(partition);
                break;
            default:
                break;
            }
        } else { // Not busy file system
            switch (get_fs(partition.fstype).read) {
            case FS::EXTERNAL:
                p_filesystem = get_filesystem_object(partition.fstype);
                if (p_filesystem)
                    p_filesystem->set_used_sectors(partition);
                break;
#ifdef HAVE_LIBPARTED_FS_RESIZE
            case FS::LIBPARTED:
                if (lp_disk)
                    LP_set_used_sectors(partition, lp_disk);
                break;
#endif
            default:
                break;
            }
        }

        Sector unallocated;
        // Only confirm that the above code succeeded in setting the sector usage
        // values for this base Partition object, hence the explicit call to the
        // base Partition class sector_usage_known() method.  For LUKS this avoids
        // calling derived PartitionLUKS class sector_usage_known() which also
        // checks for known sector usage in the encrypted file system.  But that
        // wasn't set by the above code so in the case of luks/unknown would
        // produce a false positive.
        if (! partition.sector_usage_known()) {
            if (!Utils::get_filesystem_software(partition.fstype).isEmpty()) {
                QString msg("The following list of software packages is required for %1 file system support:  %2.");
                msg = msg.arg(Utils::FSTypeToString(partition.fstype)).arg(Utils::get_filesystem_software(partition.fstype));
                qDebug() << __FUNCTION__ << msg;
            }

        } else if ((unallocated = partition.get_sectors_unallocated()) > 0) {
            /* TO TRANSLATORS: looks like   1.28GiB of unallocated space within the partition. */
            QString temp("%1 of unallocated space within the partition.");
            temp = temp.arg(Utils::format_size(unallocated, partition.sector_size));
            FS fs = get_fs(partition.fstype);
            if (fs.check != FS::NONE && fs.grow != FS::NONE) {
                temp.append("To grow the file system to fill the partition, select the partition and choose the menu item:\n");
                temp.append("Partition --> Check.");
            }
        }

        if (filesystem_resize_disallowed(partition)) {
//            QString temp = get_filesystem_object(partition.fstype)
//                                 ->get_custom_text(CTEXT_RESIZE_DISALLOWED_WARNING);
        }
    } else {
        // Set usage of mounted but unsupported file systems.
        if (partition.busy)
            mounted_fs_set_used_sectors(partition);
    }
}

void PartedCore::mounted_fs_set_used_sectors(Partition &partition)
{
    if (partition.get_mountpoints().size() > 0 && MountInfo::is_dev_mounted(partition.get_path())) {
        Byte_Value fs_size ;
        Byte_Value fs_free ;
        if (Utils::get_mounted_filesystem_usage(partition .get_mountpoint(), fs_size, fs_free) == 0)
            partition .set_sector_usage(fs_size / partition .sector_size,
                                        fs_free / partition .sector_size) ;
    }
}

void PartedCore::set_device_partitions(Device &device, PedDevice *lp_device, PedDisk *lp_disk)
{
    int EXT_INDEX = -1 ;
    device .partitions .clear() ;

    PedPartition *lp_partition = ped_disk_next_partition(lp_disk, NULL) ;
    while (lp_partition) {
        Partition *partition_temp = NULL;
        bool partition_is_busy = false ;
        FSType fstype = FS_UNKNOWN;
        QString partition_path;
        switch (lp_partition ->type) {
        case PED_PARTITION_NORMAL:
        case PED_PARTITION_LOGICAL:
            fstype = detect_filesystem(lp_device, lp_partition);
            partition_path = get_partition_path(lp_partition);
            partition_is_busy = is_busy(fstype, partition_path);
//            if (fstype == FS_LUKS)
//                partition_temp = new PartitionLUKS();
//            else
            partition_temp = new Partition();
            partition_temp->Set(device .m_path,
                                partition_path,
                                lp_partition->num,
                                (lp_partition->type == PED_PARTITION_NORMAL) ? TYPE_PRIMARY
                                : TYPE_LOGICAL,
                                fstype,
                                lp_partition->geom.start,
                                lp_partition->geom.end,
                                device.sector_size,
                                (lp_partition->type == PED_PARTITION_LOGICAL),
                                partition_is_busy);

            set_flags(*partition_temp, lp_partition);

            //if (fstype == FS_LUKS)
            // set_luks_partition(*dynamic_cast<PartitionLUKS *>(partition_temp));

            if (partition_temp->busy && partition_temp->partition_number > device.highest_busy)
                device.highest_busy = partition_temp->partition_number;
            break ;

        case PED_PARTITION_EXTENDED:
            partition_path = get_partition_path(lp_partition);

            partition_temp = new Partition();
            partition_temp->Set(device.m_path,
                                partition_path,
                                lp_partition->num,
                                TYPE_EXTENDED,
                                FS_EXTENDED,
                                lp_partition->geom.start,
                                lp_partition->geom.end,
                                device.sector_size,
                                false,
                                false);

            set_flags(*partition_temp, lp_partition);

            EXT_INDEX = device .partitions .size() ;
            break ;

        default:
            // Ignore libparted reported partitions with other type
            // bits set.
            break;
        }

        // Only for libparted reported partition types that we care about: NORMAL,
        // LOGICAL, EXTENDED
        if (partition_temp != NULL) {
            set_partition_label_and_uuid(*partition_temp);
            set_mountpoints(*partition_temp);
            set_used_sectors(*partition_temp, lp_disk);

            // Retrieve partition name
            if (device.partition_naming_supported())
                partition_temp->name = ped_partition_get_name(lp_partition);

            if (! partition_temp->inside_extended)
                device.partitions.push_back_adopt(partition_temp);
            else
                device.partitions[EXT_INDEX].logicals.push_back_adopt(partition_temp);
        }

        //next partition (if any)
        lp_partition = ped_disk_next_partition(lp_disk, lp_partition) ;
    }

    if (EXT_INDEX > -1) {
        insert_unallocated(device.m_path,
                           device .partitions[ EXT_INDEX ] .logicals,
                           device .partitions[ EXT_INDEX ] .sector_start,
                           device .partitions[ EXT_INDEX ] .sector_end,
                           device .sector_size,
                           true) ;

        //Set busy status of extended partition if and only if
        //  there is at least one busy logical partition.
        for (unsigned int t = 0 ; t < device .partitions[ EXT_INDEX ] .logicals .size() ; t ++) {
            if (device .partitions[ EXT_INDEX ] .logicals[ t ] .busy) {
                device .partitions[ EXT_INDEX ] .busy = true ;
                break ;
            }
        }
    }

    insert_unallocated(device .m_path, device .partitions, 0, device .length - 1, device .sector_size, false) ;
}

bool PartedCore::flush_device(PedDevice *lp_device)
{
    bool success = false ;
    if (ped_device_open(lp_device)) {
        success = ped_device_sync(lp_device) ;
        ped_device_close(lp_device) ;
        // (!46) Wait for udev rules to complete after this ped_device_open() and
        // ped_device_close() pair to avoid busy /dev/DISK entry when running file
        // system specific querying commands on the whole disk device in the call
        // sequence after get_device() in set_device_from_disk().
        settle_device(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS);
    }
    return success ;
}

void PartedCore::settle_device(std::time_t timeout)
{
    //如果支持udevadm
    //udevadm settle [options]　　查看udev事件队列，如果所有事件全部处理完就退出。timeout超时时间
    if (udevadm_found) {
        QString out, err;
        Utils::executcmd(QString("udevadm settle --timeout=%1").arg(timeout), out, err) ;
    } else
        sleep(timeout) ;
}

bool PartedCore::commit_to_os(PedDisk *lp_disk, time_t timeout)
{
    bool succes ;
    succes = ped_disk_commit_to_os(lp_disk) ;
    // Wait for udev rules to complete and partition device nodes to settle from above
    // ped_disk_commit_to_os() initiated kernel update of the partitions.
    settle_device(timeout) ;
    return succes ;
}

FSType PartedCore::detect_filesystem(PedDevice *lp_device, PedPartition *lp_partition)
{
    QString fsname = "";
    QString path;
    // Will query whole disk device using methods: (Q1) RAID, (Q2) blkid,
    // (Q4) internal
    if (lp_partition)
        path = get_partition_path(lp_partition);
    else
        path = lp_device->path;

    fsname = FsInfo::get_fs_type(path);
    FSType fstype = FS_UNKNOWN;
    if (fsname.isEmpty() && lp_partition && lp_partition->fs_type)
        fsname = lp_partition->fs_type->name;
    if (! fsname.isEmpty()) {
        fstype = Utils::StringToFSType(fsname);
        qDebug() << fstype;
        if (fstype != FS_UNKNOWN)
            return fstype;
    }

    fstype = detect_filesystem_internal(path, lp_device->sector_size);
    if (fstype != FS_UNKNOWN)
        return fstype;

    //no file system found....
    QString temp("Unable to detect file system! Possible reasons are:\n- ") ;
    temp.append("The file system is damaged \n- ");
    temp.append("The file system is unknown to GParted \n-");
    temp.append("There is no file system available (unformatted) \n- ");
    temp.append(QString("The device entry %1 is missing").arg(path));
    qDebug() << __FUNCTION__ << temp;
    return FS_UNKNOWN;
}

FSType PartedCore::detect_filesystem_internal(const QString &path, Byte_Value sector_size)
{
    char magic1[16];  // Big enough for largest signatures[].sig1 or sig2
    char magic2[16];
    FSType fstype = FS_UNKNOWN;

    char *buf = static_cast<char *>(malloc(sector_size));
    if (! buf)
        return FS_UNKNOWN;

    int fd = open(path.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        free(buf);
        return FS_UNKNOWN;
    }

    struct {
        Byte_Value   offset1;
        const char *sig1;
        Byte_Value   offset2;
        const char *sig2;
        FSType       fstype;
    } signatures[] = {
        //offset1, sig1              , offset2, sig2  , fstype
        {     0LL, "LUKS\xBA\xBE",     0LL, NULL, FS_LUKS           },
        {     3LL, "-FVE-FS-",     0LL, NULL, FS_BITLOCKER      },
        {     0LL, "\x52\x56\xBE\x1B",     0LL, NULL, FS_GRUB2_CORE_IMG },
        {     0LL, "\x52\x56\xBE\x6F",     0LL, NULL, FS_GRUB2_CORE_IMG },
        {     0LL, "\x52\xE8\x28\x01",     0LL, NULL, FS_GRUB2_CORE_IMG },
        {     0LL, "\x52\xBF\xF4\x81",     0LL, NULL, FS_GRUB2_CORE_IMG },
        {     0LL, "\x52\x56\xBE\x63",     0LL, NULL, FS_GRUB2_CORE_IMG },
        {     0LL, "\x52\x56\xBE\x56",     0LL, NULL, FS_GRUB2_CORE_IMG },
        {    24LL, "\x01\x00",    32LL, "NXSB", FS_APFS           },
        {   512LL, "LABELONE",   536LL, "LVM2", FS_LVM2_PV        },
        {  1030LL, "\x34\x34",     0LL, NULL, FS_NILFS2         },
        { 65536LL, "ReIsEr4",     0LL, NULL, FS_REISER4        },
        { 65600LL, "_BHRfS_M",     0LL, NULL, FS_BTRFS          }
    };
    // For simple BitLocker recognition consider validation of BIOS Parameter block
    // fields unnecessary.
    // *   Detecting BitLocker
    //     http://blogs.msdn.com/b/si_team/archive/2006/10/26/detecting-bitlocker.aspx
    //
    // Recognise GRUB2 core.img just by any of the possible first 4 bytes of x86 CPU
    // instructions it starts with.
    // *   bootinfoscript v0.77 line 1990  [GRUB2 core.img possible staring 4 bytes]
    //     https://github.com/arvidjaar/bootinfoscript/blob/009f509d59e2f0d39b8d44692e2a81720f5af7b6/bootinfoscript#L1990
    //
    // Simple APFS recognition based on matching the following fields in the
    // superblock:
    // 1)  Object type is OBJECT_TYPE_NX_SUPERBLOCK, lower 16-bits of the object type
    //     field is 0x0001 stored as little endian bytes 0x01, 0x00.
    //     WARNING: The magic signatures are defined as NUL terminated strings so the
    //     below code only does a 1-byte match for 0x01, rather than a 2-byte match
    //     for 0x01, 0x00.
    // 2)  4 byte magic "NXSB".
    // *   Apple File System Reference
    //     https://developer.apple.com/support/apple-file-system/Apple-File-System-Reference.pdf

    Byte_Value prev_read_offset = -1;
    memset(buf, 0, sector_size);

    for (unsigned int i = 0 ; i < sizeof(signatures) / sizeof(signatures[0]) ; i ++) {
        const size_t len1 = std::min((signatures[i].sig1 == NULL) ? 0U : strlen(signatures[i].sig1),
                                     sizeof(magic1));
        const size_t len2 = std::min((signatures[i].sig2 == NULL) ? 0U : strlen(signatures[i].sig2),
                                     sizeof(magic2));
        // NOTE: From this point onwards signatures[].sig1 and .sig2 are treated
        // as character buffers of known lengths len1 and len2, not NUL terminated
        // strings.
        if (len1 == 0UL || (signatures[i].sig2 != NULL && len2 == 0UL))
            continue;  // Don't allow 0 length signatures to match

        Byte_Value read_offset = signatures[i].offset1 / sector_size * sector_size;

        // Optimisation: only read new sector when it is different to the
        // previously read sector.
        if (read_offset != prev_read_offset) {
            if (lseek(fd, read_offset, SEEK_SET) == read_offset &&
                    read(fd, buf, sector_size)       == sector_size) {
                prev_read_offset = read_offset;
            } else {
                // Outside block device boundaries or other error.
                continue;
            }
        }

        memcpy(magic1, buf + signatures[i].offset1 % sector_size, len1);

        // WARNING: This assumes offset2 is in the same sector as offset1
        if (signatures[i].sig2 != NULL)
            memcpy(magic2, buf + signatures[i].offset2 % sector_size, len2);

        if (memcmp(magic1, signatures[i].sig1, len1) == 0                                 &&
                (signatures[i].sig2 == NULL || memcmp(magic2, signatures[i].sig2, len2) == 0)) {
            fstype = signatures[i].fstype;
            break;
        }
    }

    close(fd);
    free(buf);

    return fstype;
}

QString PartedCore::get_partition_path(PedPartition *lp_partition)
{
    char *lp_path;   //we have to free the result of ped_partition_get_path()
    QString partition_path("Partition path not found");

    lp_path = ped_partition_get_path(lp_partition);
    if (lp_path != NULL) {
        partition_path = lp_path;
        free(lp_path);
    }
    return partition_path ;
}

DeviceInfo PartedCore::getDeviceinfo()
{
    qDebug() << __FUNCTION__ << devices.size() << "^^^^^^^";
    DeviceInfo info = devices.at(0);
    qDebug() << __FUNCTION__ << "#########";
    qDebug() << info.m_path << info.heads << info.cylinders << info.serial_number << info.max_prims;
    return  info;
}

DeviceInfoMap PartedCore::getAllDeviceinfo()
{
    return inforesult;
}



}//end namespace