/****************************************************************************
**
** Copyright (C) 2016 Philip Seeger
** This file is part of CapacityTester.
**
** CapacityTester is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** CapacityTester is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with CapacityTester. If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

#include "volumetester.hpp"

/*! \class VolumeTester
 *
 * \brief The VolumeTester class provides an interface for
 * testing a mounted filesystem.
 *
 * A VolumeTester can test the capacity of a mounted filesystem
 * by writing files to it and verifying those files.
 * The filesystem should be completely empty.
 * A filesystem that's already half full cannot be tested properly.
 *
 * The goal is to detect a fake USB thumbdrive (or memory card).
 * There are many counterfeit flash storage devices or memory cards
 * available online.
 * These claim to have a higher capacity than they really have.
 * For example, a thumbdrive sold as "16 GB USB drive"
 * will present itself as a storage device holding 16 GB
 * but in reality, it might just have a 4 GB chip.
 * Writes beyond the 4 GB limit are usually ignored
 * without reporting an error.
 * This class will fill the filesystem completely and then verify it
 * to see if the returned data is correct.
 *
 * Before testing every single byte (within the available space),
 * a quick test is performed, which will try to access certain offsets.
 * This will only take a couple of seconds.
 * If the quick test fails, it's already clear that the storage device
 * is bad, although the exact limit will not be known yet.
 *
 * This class works on a mounted filesystem rather than a storage device.
 * This filesystem should span the entire storage device,
 * i.e. it should be the only filesystem on that device.
 *
 * As a courtesy to the user, this tester will clean up after itself
 * and remove all test files afterwards.
 *
 */

#if defined(_WIN32) && !defined(NO_FSYNC)
int
fsync(int fd)
{
    /*
     * Emulate fsync on platforms which lack it, primarily Windows and
     * cross-compilers like MinGW.
     *
     * This is derived from sqlite3 sources and is in the public domain.
     *
     * Written by Richard W.M. Jones <rjones.at.redhat.com>
     */

    HANDLE h = (HANDLE)_get_osfhandle(fd);
    DWORD err;

    if (h == INVALID_HANDLE_VALUE)
    {
        errno = EBADF;
        return -1;
    }

    if (!FlushFileBuffers(h))
    {
        /*
         * Translate some Windows errors into rough approximations of Unix
         * errors.  MSDN is useless as usual - in this case it doesn't
         * document the full range of errors.
         */
        err = GetLastError();
        switch (err)
        {
            /* eg. Trying to fsync a tty. */
            case ERROR_INVALID_HANDLE:
            errno = EINVAL;
            break;

            default:
            errno = EIO;
        }
        return -1;
    }

    return 0;
}
#endif

/*!
 * Checks if the provided string is a valid mountpoint.
 */
bool
VolumeTester::isValid(const QString &mountpoint)
{
    //Mountpoint defined
    if (mountpoint.isEmpty()) return false; //no, don't default to cwd

    //Check if it's (still) a mountpoint
    QStorageInfo storage(mountpoint);
    if (!storage.isValid() || storage.rootPath() != mountpoint)
    {
        //Not a valid mountpoint
        //Might be an unmounted directory (/mnt/tmp), but no mountpoint
        return false;
    }
    if (!storage.isReady())
    {
        return false;
    }

    return true;
}

/*!
 * Returns a list of available mountpoints.
 */
QStringList
VolumeTester::availableMountpoints()
{
    QStringList mountpoints;

    foreach (const QStorageInfo &storage, QStorageInfo::mountedVolumes())
    {
        QString mountpoint = storage.rootPath();
        if (!storage.isValid()) continue;
        mountpoints << mountpoint;
    }

    return mountpoints;
}

/*!
 * Constructs a VolumeTester for the specified mountpoint.
 *
 * Use availableMountpoints() to get a list of available mountpoints.
 */
VolumeTester::VolumeTester(const QString &mountpoint)
            : block_size_max(16 * MB),
              file_size_max(512 * MB),
              file_prefix("CAPACITYTESTER"),
              bytes_total(0),
              bytes_written(0),
              bytes_remaining(0),
              _canceled(false),
              error_type(Error::Unknown)
{
    //Apply mountpoint if valid
    if (isValid(mountpoint))
    {
        _mountpoint = mountpoint;
    }

}

/*!
 * Checks if this VolumeTester is still valid, i.e.,
 * if it still points to a valid mountpoint.
 */
bool
VolumeTester::isValid()
const
{
    return isValid(mountpoint());
}

/*!
 * Returns the mountpoint used by this VolumeTester.
 */
QString
VolumeTester::mountpoint()
const
{
    QString mountpoint;
    mountpoint = _mountpoint;
    return mountpoint;
}

/*!
 * Returns the total capacity of the filesystem.
 */
qint64
VolumeTester::bytesTotal()
const
{
    qint64 bytes = 0;

    QStorageInfo storage(mountpoint());
    if (storage.isValid() && storage.isReady())
    {
        bytes = storage.bytesTotal();
    }

    return bytes;
}

/*!
 * Returns the number of bytes used on the filesystem.
 */
qint64
VolumeTester::bytesUsed()
const
{
    qint64 bytes = 0;

    QStorageInfo storage(mountpoint());
    if (storage.isValid() && storage.isReady())
    {
        bytes = storage.bytesTotal() - storage.bytesFree();
    }

    return bytes;
}

/*!
 * Returns the available space on the filesystem, in bytes.
 */
qint64
VolumeTester::bytesAvailable()
const
{
    qint64 bytes = 0;

    QStorageInfo storage(mountpoint());
    if (storage.isValid() && storage.isReady())
    {
        bytes = storage.bytesAvailable();
    }

    return bytes;
}

/*!
 * Returns the name of the filesystem, if defined.
 * Returns an empty string otherwise.
 */
QString
VolumeTester::name()
const
{
    QString name;

    QStorageInfo storage(mountpoint());
    if (storage.isValid() && storage.isReady())
    {
        name = storage.name();
    }

    return name;
}

/*!
 * Returns a combination of mountpoint and filesystem name.
 */
QString
VolumeTester::label()
const
{
    QString label;

    if (isValid())
    {
        label = mountpoint();
        QString name = this->name();
        if (!name.isEmpty())
            label = label + ": " + name;
    }

    return label;
}

/*!
 * Returns a list of QFileInfo objects, one for every file.
 * This list should be empty before a test is started.
 */
QFileInfoList
VolumeTester::entryInfoList()
const
{
    QFileInfoList files;
    if (!isValid()) return files;

    //List files in filesystem root (not recursively)
    QDir dir(mountpoint());
    files = dir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries |
        QDir::Hidden | QDir::System,
        QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);

    return files;
}

/*!
 * Returns a string list of file names.
 * See entryInfoList().
 */
QStringList
VolumeTester::rootFiles()
const
{
    QStringList file_name_list;

    foreach (QFileInfo fi, entryInfoList())
    {
        QString name = fi.fileName();
        if (fi.isDir()) name += "/";
        file_name_list << name;
    }

    return file_name_list;
}

/*!
 * Returns a list of conflicting files.
 * It's not possible to start a test with those files present.
 *
 * If the program crashes during a test, the test files are not cleaned up
 * and are then considered conflict files.
 */
QStringList
VolumeTester::conflictFiles()
const
{
    QStringList conflict_files;

    assert(!file_prefix.isEmpty());

    foreach (QString name, rootFiles())
    {
        if (name.startsWith(file_prefix))
            conflict_files << name;
    }

    return conflict_files;
}

/*!
 * Starts a test.
 * A test consists of three phases:
 * 1. Initialization: The test files are created and
 * a quick test is performed.
 * 2. Write: A test pattern is written to the files.
 * 3. Verify: The files are read and compared with the pattern.
 */
void
VolumeTester::start()
{
    //Abort if mountpoint not valid anymore
    if (!isValid())
    {
        emit failed();
        emit finished();
        return;
    }

    //Test phases:
    //1 Initialization (write first and last block bytes)
    //2 Full write
    //3 Full read

    //Test files and blocks:
    //The available space is filled with test files.
    //Each file is grown to a size of file_size_max bytes
    //except for the last one, which may be smaller.
    //After the initialization, the files are filled with test data,
    //one block at a time.
    //The block size is a multiple of 1 MB (16 MB at the time of writing),
    //smaller than a file.
    //The last block in the last file may be smaller than block_size_max.

    //Block size multiple of 1024
    assert(block_size_max > 0 && block_size_max % MB == 0);

    //File size > block size, like 512 MB
    assert(file_size_max > 0 && file_size_max % MB == 0);
    assert(file_size_max > block_size_max);

    //Size of volume
    bytes_total = bytesAvailable();
    bytes_written = 0;
    bytes_remaining = bytes_total;
    if (bytes_total <= 0)
    {
        //Volume full or error getting size
        emit failed(Error::Full);
        emit finished();
        return;
    }

    //Test pattern
    generateTestPattern();
    assert(pattern.size() == block_size_max);

    //Calculate file and block sizes
    {
        int file_count = bytes_total / file_size_max;
        int last_file_size = bytes_total % file_size_max;
        if (last_file_size) file_count++;
        QDir dir(mountpoint());
        file_infos.clear();
        for (int i = 0; i < file_count; i++)
        {
            //File size
            int size = file_size_max; //e.g., 512 MB
            if (i == file_count - 1 && last_file_size)
                size = last_file_size;
            assert(size > 0);

            //File area
            //*size_max are long (long) (not just int) to prevent overflows
            qint64 pos = i * file_size_max; //NOT times current size!
            assert(pos >= 0); //int overflow may lead to negative pos
            qint64 end = pos + size;

            //File path
            QString name = file_prefix + QString::number(i);
            QString path = dir.absoluteFilePath(name);

            //File ID
            QByteArray id_bytes = QString::number(i).toUtf8();
            id_bytes.append((char)'\1');

            //File information
            FileInfo file_info;
            file_info.path = path;
            file_info.offset = pos;
            file_info.size = size;
            file_info.end = end;
            file_info.id = id_bytes;

            //Blocks
            int block_count = size / block_size_max;
            int last_block_size = size % block_size_max;
            if (last_block_size) block_count++;
            for (int j = 0; j < block_count; j++)
            {
                //Block size
                int block_size = block_size_max; //e.g., 16 MB
                if (j == block_count - 1 && last_block_size)
                    block_size = last_block_size;
                assert(block_size > 0);

                //Block position
                qint64 pos = j * block_size_max; //NOT times current size!
                assert(pos >= 0);
                qint64 end = pos + block_size;

                //Block ID
                QByteArray id_bytes = QString("%1:%2").arg(i).arg(j).toUtf8();
                id_bytes.append((char)'\1');

                //Block information
                BlockInfo block_info;
                block_info.rel_offset = pos; //relative offset within file
                block_info.abs_offset = file_info.offset + pos; //absolute
                block_info.size = block_size;
                block_info.abs_end = file_info.offset + end;
                block_info.id = id_bytes;

                //Add to list
                file_info.blocks << block_info;
            }
            assert(file_info.blocks.size() == block_count);

            //Add to list
            file_infos << file_info;
        }
    }

    //File objects (local within the scope of this function)
    //QFile objects created on heap, auto-deleted when (parent) out of scope
    QObject files_parent; //restrict QFile objects to this function
    connect(&files_parent,
            SIGNAL(destroyed()),
            SLOT(deleteFiles()));
    QList<QPointer<QFile>> files;
    for (int i = 0, ii = file_infos.size(); i < ii; i++)
    {
        FileInfo file_info = file_infos[i];

        //Create file
        QFile *file = new QFile(file_info.path, &files_parent); //with parent
        files << file;
    }

    //Run tests
    if (initialize(files) && writeFull(files) && verifyFull(files))
    {
        //Test succeeded
        emit succeeded();
        emit finished();
    }
    else
    {
        //Test failed
        emit failed(error_type);
        emit finished();
    }

}

/*!
 * Requests the currently running test to be aborted gracefully.
 * This will wait for the current file operation to complete.
 * The test files will be deleted normally.
 */
void
VolumeTester::cancel()
{
    error_type |= Error::Aborted;
    _canceled = true;
}

bool
VolumeTester::initialize(const QList<QPointer<QFile>> &files)
{
    assert(files.size() == file_infos.size());

    //Start
    emit initializationStarted(bytes_total);

    //Create test files to fill available space
    //Last test file usually smaller (to fill space)
    uchar byte_fe = 254;
    QElapsedTimer timer_initializing;
    double initialized_mb = 0;
    double initialized_sec = 0;
    for (int i = 0, ii = file_infos.size(); i < ii; i++)
    {
        FileInfo file_info = file_infos[i];
        QFile *file = files[i];

        //Create file
        if (!file->open(QIODevice::ReadWrite))
        {
            //Creating test file failed
            error_type |= Error::Create;
            if (file->error() & QFileDevice::PermissionsError)
                error_type |= Error::Permissions;
            emit createFailed(i, file_info.offset);
            return false;
        }

        //Start timer
        timer_initializing.start();

        //Write id
        //Usually 1 byte but could be longer
        if (!file->seek(0) ||
            file->write(file_info.id) != file_info.id.size())
        {
            //Writing id failed
            error_type |= Error::Write;
            emit writeFailed(file_info.offset, file_info.size);
            return false;
        }

        //Grow file to calculated size
        if (!file->resize(file_info.size))
        {
            //Growing file failed
            error_type |= Error::Write;
            error_type |= Error::Resize;
            emit writeFailed(file_info.offset, file_info.size);
            return false;
        }

        //Write last byte
        if (!file->seek(file_info.size - 1) || !file->putChar(byte_fe))
        {
            //Writing last byte failed
            error_type |= Error::Write;
            emit writeFailed(file_info.offset, file_info.size);
            return false;
        }

        //Block initialized, get time
        initialized_sec += timer_initializing.elapsed() / 1000;
        initialized_mb += file_info.size / MB;
        double avg_speed =
            initialized_sec ? initialized_mb / initialized_sec : 0;
        emit initialized(file_info.end, avg_speed);

        //Verify this file right away to speed things up
        //Don't wait for last file to be written if second already file lost

        //Verify last byte
        char c;
        if (!file->seek(file_info.size - 1) ||
            !file->getChar(&c) || (uchar)c != byte_fe)
        {
            //Verifying last byte failed
            error_type |= Error::Verify;
            emit verifyFailed(file_info.offset, file_info.size);
            return false;
        }

        //Verify id
        if (!file->seek(0) ||
            file->read(file_info.id.size()) != file_info.id)
        {
            //Verifying id failed
            error_type |= Error::Verify;
            emit verifyFailed(file_info.offset, file_info.size);
            return false;
        }

        //Cancel gracefully
        if (abortRequested()) return false;
    }
    assert(files.size() == file_infos.size());

    //Verify all files (quick test, just first and last few bytes)
    for (int i = 0, ii = file_infos.size(); i < ii; i++)
    {
        FileInfo file_info = file_infos[i];
        QFile *file = files[i];

        //Verify last byte
        char c;
        if (!file->seek(file_info.size - 1) ||
            !file->getChar(&c) || (uchar)c != byte_fe)
        {
            //Verifying last byte failed
            error_type |= Error::Verify;
            emit verifyFailed(file_info.offset, file_info.size);
            return false;
        }

        //Verify id
        if (!file->seek(0) ||
            file->read(file_info.id.size()) != file_info.id)
        {
            //Verifying id failed
            error_type |= Error::Verify;
            emit verifyFailed(file_info.offset, file_info.size);
            return false;
        }

        //Cancel gracefully
        if (abortRequested()) return false;
    }

    return true;
}

bool
VolumeTester::writeFull(const QList<QPointer<QFile>> &files)
{
    assert(files.size() == file_infos.size());

    //Start
    emit writeStarted();

    //Write test pattern
    QElapsedTimer timer_writing;
    double written_mb = 0;
    double written_sec = 0;
    for (int i = 0, ii = file_infos.size(); i < ii; i++)
    {
        FileInfo file_info = file_infos[i];
        QFile *file = files[i];

        //Flush cache
        //Might block for a while if initialized files not on disk yet (cache)
        #ifdef USE_FSYNC
        fsync(file->handle());
        #endif

        //Write blocks
        for (int j = 0, jj = file_info.blocks.size(); j < jj; j++)
        {
            BlockInfo block_info = file_info.blocks[j];

            //Block data (based on pattern, with unique id)
            QByteArray block = blockData(i, j);

            //Start timer
            timer_writing.start();

            //Write block
            if (!file->seek(block_info.rel_offset) ||
                file->write(block) != block_info.size)
            {
                //Writing chunk failed
                error_type |= Error::Write;
                emit writeFailed(block_info.abs_offset, block_info.size);
                return false;
            }

            //Flush cache
            #ifdef USE_FSYNC
            fsync(file->handle());
            #endif

            //Block written
            written_sec += timer_writing.elapsed() / 1000;
            written_mb += block_info.size / MB;
            double avg_speed = written_sec ? written_mb / written_sec : 0;
            emit written(block_info.abs_end, avg_speed);

            //Cancel gracefully
            if (abortRequested()) return false;
        }
    }

    return true;
}

bool
VolumeTester::verifyFull(const QList<QPointer<QFile>> &files)
{
    assert(files.size() == file_infos.size());

    //Read test pattern
    emit verifyStarted();
    QElapsedTimer timer_verifying;
    double verified_mb = 0;
    double verified_sec = 0;
    for (int i = 0, ii = file_infos.size(); i < ii; i++)
    {
        FileInfo file_info = file_infos[i];
        QFile *file = files[i];

        //Flush cache
        #ifdef USE_FSYNC
        fsync(file->handle());
        #endif

        //Read pattern in small chunks
        for (int j = 0, jj = file_info.blocks.size(); j < jj; j++)
        {
            BlockInfo block_info = file_info.blocks[j];

            //Block data (based on pattern, with unique id)
            QByteArray block = blockData(i, j);

            //Start timer
            timer_verifying.start();

            //Read block
            if (!file->seek(block_info.rel_offset) ||
                file->read(block_info.size) != block)
            {
                //Verifying chunk failed
                error_type |= Error::Verify;
                emit verifyFailed(block_info.abs_offset, block_info.size);
                return false;
            }

            //Block verified
            verified_sec += timer_verifying.elapsed() / 1000;
            verified_mb += block_info.size / MB;
            double avg_speed = verified_sec ? verified_mb / verified_sec : 0;
            emit verified(block_info.abs_end, avg_speed);

            //Cancel gracefully
            if (abortRequested()) return false;
        }
    }

    return true;
}

void
VolumeTester::generateTestPattern()
{
    //Test pattern
    //Pattern size < block size
    int pattern_size = block_size_max; //for example 16 MB
    assert(pattern_size > 0);
    QByteArray new_pattern(pattern_size, (char)0);
    srand(time(0));
    for (int i = 0; i < pattern_size; i++)
    {
        //Random byte except 0
        uchar byte = rand() % 254 + 1; //0 < byte < 255
        new_pattern[i] = byte;
    }
    this->pattern = new_pattern;
    assert(this->pattern.size() == pattern_size);

}

void
VolumeTester::deleteFiles()
{
    for (int i = file_infos.size(); --i >= 0;)
    {
        FileInfo file_info = file_infos[i];
        QFile file(file_info.path);
        file.remove();
        file_infos.removeAt(i);
    }
    assert(file_infos.isEmpty());

}

QByteArray
VolumeTester::blockData(int file_index, int block_index)
const
{
    FileInfo file_info = file_infos.at(file_index);
    BlockInfo block_info = file_info.blocks.at(block_index);

    //Block data based on test pattern
    QByteArray block = pattern;
    assert(!block.isEmpty()); //pattern must have been generated previously

    //Shrink last block
    if (block.size() > block_info.size)
        block.resize(block_info.size);
    assert(block.size() == block_info.size);
    assert(!block.isEmpty());

    //Put unique id sequence at beginning (if possible)
    if (block_info.size >= block_info.id.size())
    {
        block.replace(0, block_info.id.size(), block_info.id);
    }
    assert(block.size() == block_info.size);

    return block;
}

bool
VolumeTester::abortRequested()
const
{
    return _canceled;
}
