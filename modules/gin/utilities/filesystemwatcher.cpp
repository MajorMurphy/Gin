/*==============================================================================

Copyright 2018 by Roland Rabien
For more information visit www.rabiensoftware.com

==============================================================================*/

//==============================================================================
#if JUCE_MAC
class FileSystemWatcher::Impl
{
public:
    Impl (FileSystemWatcher& o, File f) : owner (o), folder (f)
    {
        NSString* newPath = [NSString stringWithUTF8String:folder.getFullPathName().toRawUTF8()];

        paths = [[NSArray arrayWithObject:newPath] retain];
        context.version         = 0L;
        context.info            = this;
        context.retain          = nil;
        context.release         = nil;
        context.copyDescription = nil;

        stream = FSEventStreamCreate (kCFAllocatorDefault, callback, &context, (CFArrayRef)paths, kFSEventStreamEventIdSinceNow, 1.0, kFSEventStreamCreateFlagUseCFTypes);
        if (stream)
        {
            FSEventStreamScheduleWithRunLoop (stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            FSEventStreamStart (stream);
        }

    }

    ~Impl()
    {
        if (stream)
        {
            FSEventStreamStop (stream);
            FSEventStreamUnscheduleFromRunLoop (stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            FSEventStreamInvalidate (stream);
            FSEventStreamRelease (stream);
        }
    }

    static void callback (ConstFSEventStreamRef streamRef, void* clientCallBackInfo, size_t numEvents, void* eventPaths, const FSEventStreamEventFlags* eventFlags,
                   const FSEventStreamEventId* eventIds)
    {
        ignoreUnused (streamRef, numEvents, eventIds, eventPaths, eventFlags);
        
        Impl* impl = (Impl*)clientCallBackInfo;
        impl->owner.folderChanged (impl->folder);
    }

    FileSystemWatcher& owner;
    File folder;

    NSArray* paths;
    FSEventStreamRef stream;
    struct FSEventStreamContext context;
};
#endif

//==============================================================================
#ifdef JUCE_LINUX
#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

class FileSystemWatcher::Impl : public Thread,
                                private AsyncUpdater
{
public:
    Impl (FileSystemWatcher& o, File f)
      : Thread ("FileSystemWatcher::Impl"), owner (o), folder (f)
    {
        fd = inotify_init();

        wd = inotify_add_watch (fd, folder.getFullPathName().toRawUTF8(), IN_ATTRIB | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | IN_MOVED_TO | IN_MOVED_FROM);

        startThread();
    }

    ~Impl()
    {
        signalThreadShouldExit();
        inotify_rm_watch (fd, wd);
        close (fd);

        waitForThreadToExit (1000);
    }

    void run() override
    {
        char buf[BUF_LEN];

        while (true)
        {
            int numRead = read (fd, buf, BUF_LEN);

            if (numRead <= 0 || threadShouldExit())
                break;

            triggerAsyncUpdate();
        }
    }

    void handleAsyncUpdate() override
    {
        owner.folderChanged (folder);
    }

    FileSystemWatcher& owner;
    File folder;

    int fd;
    int wd;
};
#endif

//==============================================================================
#ifdef _WIN32
class FileSystemWatcher::Impl : private AsyncUpdater,
                                private Thread
{
public:
    struct Event
    {
        File file;
        FileSystemEvent fsEvent;

        bool operator== (const Event& other) const
        {
            return file == other.file && fsEvent == other.fsEvent;
        }
    };

    Impl (FileSystemWatcher& o, File f)
      : Thread ("FileSystemWatcher::Impl"), owner (o), folder (f)
    {
        WCHAR path[_MAX_PATH] = {0};
        wcsncpy (path, folder.getFullPathName().toWideCharPointer(), _MAX_PATH - 1);

        folderHandle = CreateFileW (path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);


        if (folderHandle != INVALID_HANDLE_VALUE)
            startThread();
    }

    ~Impl()
    {
        if (isThreadRunning())
        {
            signalThreadShouldExit();

            CancelIoEx (folderHandle, nullptr);

            stopThread (1000);
        }

        CloseHandle (folderHandle);
    }

    void run() override
    {
        const int heapSize = 16 * 1024;
        HeapBlock<uint8> buffer (heapSize);
        memset (buffer.get(), 0, heapSize);

        DWORD bytesOut = 0;

        while (! threadShouldExit())
        {
            BOOL success = ReadDirectoryChangesW (folderHandle, buffer.get(), heapSize, false,           
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                &bytesOut, nullptr, nullptr);

            if (success && bytesOut > 0)
            {
                ScopedLock sl (lock);

                uint8* rawData = buffer.get();
                while (true)
                {
                    FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)rawData;

                    Event e;
                    e.file = folder.getChildFile (String (fni->FileName, fni->FileNameLength));

                    switch (fni->Action)
                    {
                        case FILE_ACTION_ADDED:
                        case FILE_ACTION_RENAMED_NEW_NAME:
                            e.fsEvent = fileCreated;
                            break;
                        case FILE_ACTION_MODIFIED:
                            e.fsEvent = fileUpdated;
                            break;
                        case FILE_ACTION_REMOVED:
                        case FILE_ACTION_RENAMED_OLD_NAME:
                            e.fsEvent = fileDeleted;
                            break;
                    }

                    bool duplicateEvent = false;
                    for (auto existing : events)
                    {
                        if (e == existing)
                        {
                            duplicateEvent = true;
                            break;
                        }
                    }

                    if (! duplicateEvent)
                        events.add (e);

                    if (fni->NextEntryOffset > 0)
                        rawData += fni->NextEntryOffset;
                    else
                        break;
                }

                triggerAsyncUpdate();
            }
        }
    }

    void handleAsyncUpdate() override
    {
        ScopedLock sl (lock);

        owner.folderChanged (folder);

        for (auto e : events)
            owner.fileChanged (e.file, e.fsEvent);

        events.clear();
    }

    FileSystemWatcher& owner;
    const File folder;

    CriticalSection lock;
    Array<Event> events;

    HANDLE folderHandle;

};
#endif

FileSystemWatcher::FileSystemWatcher()
{
}

FileSystemWatcher::~FileSystemWatcher()
{
}

void FileSystemWatcher::addFolder (const File& folder)
{
    watched.add (new Impl (*this, folder));
}

void FileSystemWatcher::removeFolder (const File& folder)
{
    for (int i = watched.size(); --i >= 0;)
    {
        if (watched[i]->folder == folder)
        {
            watched.remove (i);
            break;
        }
    }
}

void FileSystemWatcher::addListener (Listener* newListener)
{
    listeners.add (newListener);
}

void FileSystemWatcher::removeListener (Listener* listener)
{
    listeners.remove (listener);
}

void FileSystemWatcher::folderChanged (const File& folder)
{
    listeners.call (&FileSystemWatcher::Listener::folderChanged, folder);
}

void FileSystemWatcher::fileChanged (const File& file, FileSystemEvent fsEvent)
{
    listeners.call (&FileSystemWatcher::Listener::fileChanged, file, fsEvent);
}