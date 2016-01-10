#pragma once

#include <boost/enable_shared_from_this.hpp>	// TODO migrate to standard C++
#include <boost/filesystem.hpp>					// TODO migrate to standard C++

#include <boost/unordered_set.hpp>				// TODO migrate to standard C++

#include <deque>
#include <thread>
#include <mutex>
#include <time.h>
#include <string>

#include <windows.h>

namespace filemonitor {

class file_monitor_impl :
public boost::enable_shared_from_this<file_monitor_impl>
{
	
public:
	file_monitor_impl()
	: run_(true),
	iocp_(init_iocp()),
	work_thread_( &file_monitor_impl::work_thread, this )
	{}
	
	~file_monitor_impl()
	{
		// The work thread is stopped and joined.
		stop_work_thread();
		work_thread_.join();
	}
	
	void add_file( const boost::filesystem::path &file )
	{
		// TODO update the flags appropriately.  May not need write.  Also the open permissions may need to be changed.
		HANDLE handle = CreateFileA( file.string().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL );
        if( handle == INVALID_HANDLE_VALUE ) {
            DWORD last_error = GetLastError();
			// TODO move this out of boost namespace
			boost::system::system_error e(boost::system::error_code(last_error, boost::system::get_system_category()), "boost::asio::basic_dir_monitor_service::add_directory: CreateFile failed");
            boost::throw_exception(e);
        }

        // No smart pointer can be used as the pointer must travel as a completion key
        // through the I/O completion port module.
        completion_key *ck = new completion_key( handle, file );
        iocp_ = CreateIoCompletionPort( ck->handle, iocp_, reinterpret_cast<ULONG_PTR>(ck), 0 );
        if( iocp_ == NULL ) {
            delete ck;
            DWORD last_error = GetLastError();
			// TODO move this out of the boost namespace
            boost::system::system_error e(boost::system::error_code(last_error, boost::system::get_system_category()), "boost::asio::basic_dir_monitor_service::add_directory: CreateIoCompletionPort failed");
            boost::throw_exception(e);
        }

        DWORD bytes_transferred; // ignored
		// TODO change filter to something readable
		DWORD filter = 0x1FF;
        BOOL res = ReadDirectoryChangesW(ck->handle, ck->buffer, sizeof(ck->buffer), FALSE, filter, &bytes_transferred, &ck->overlapped, NULL);
        if( !res ) {
            delete ck;
            DWORD last_error = GetLastError();
			// TODO move this out of the boost namespace
            boost::system::system_error e(boost::system::error_code(last_error, boost::system::get_system_category()), "boost::asio::basic_dir_monitor_service::add_directory: ReadDirectoryChangesW failed");
            boost::throw_exception(e);
        }

		std::lock_guard<std::mutex> lock( paths_mutex_ );
		paths_.insert( file );
	}
	
	void remove_file( const boost::filesystem::path &file )
	{
		std::lock_guard<std::mutex> lock( paths_mutex_ );
		paths_.erase( file );
	}
	
	void destroy()
	{
		std::lock_guard<std::mutex> lock( events_mutex_ );
		run_ = false;
		events_cond_.notify_all();
	}
	
	file_monitor_event popfront_event( boost::system::error_code &ec )
	{
		std::unique_lock<std::mutex> lock( events_mutex_ );
		while( run_ && events_.empty() ) {
			events_cond_.wait( lock );
		}
		file_monitor_event ev;
		if( ! events_.empty() ) {
			ec = boost::system::error_code();
			ev = events_.front();
			events_.pop_front();
		} else {
			ec = boost::asio::error::operation_aborted;
		}
		return ev;
	}
	
	void pushback_event( const file_monitor_event &ev )
	{
		std::lock_guard<std::mutex> lock( events_mutex_ );
		if( run_ ) {
			events_.push_back( ev );
			events_cond_.notify_all();
		}
	}
	
private:

	struct completion_key
    {
        completion_key( HANDLE h, const boost::filesystem::path& p )
        : handle(h),
        path(p)
        {
            ZeroMemory(&overlapped, sizeof(overlapped));
        }

        HANDLE handle;
		boost::filesystem::path path;
        char buffer[1024];
        OVERLAPPED overlapped;
    };
	
    HANDLE init_iocp()
    {
        HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (iocp == NULL)
        {
            DWORD last_error = GetLastError();
			// TODO move this out of the boost namespace
            boost::system::system_error e(boost::system::error_code(last_error, boost::system::get_system_category()), "boost::asio::basic_dir_monitor_service::init_iocp: CreateIoCompletionPort failed");
            boost::throw_exception(e);
        }
        return iocp;
    }

	void work_thread()
	{
		while( running() ) {
			DWORD bytes_transferred;
			completion_key *ck;
            OVERLAPPED *overlapped;
            BOOL res = GetQueuedCompletionStatus( iocp_, &bytes_transferred, reinterpret_cast<PULONG_PTR>(&ck), &overlapped, INFINITE );
            if( !res )
            {
                DWORD last_error = GetLastError();
                // TODO move this out of the boost namespace
				boost::system::system_error e(boost::system::error_code(last_error, boost::system::get_system_category()), "boost::asio::basic_dir_monitor_service::work_thread: GetQueuedCompletionStatus failed");
                boost::throw_exception(e);
            }

			// TODO we may leak completion_keys if we exit while watching multiple files.  Need to check this

            if( ck )
            {
                // If a file handle is closed GetQueuedCompletionStatus() returns and bytes_transferred will be set to 0.
                // The completion key must be deleted then as it won't be used anymore.
                if( !bytes_transferred ) {
                    delete ck;
					continue;
                }

				DWORD offset = 0;
                PFILE_NOTIFY_INFORMATION fni;
                do
                {
                    fni = reinterpret_cast<PFILE_NOTIFY_INFORMATION>( ck->buffer + offset );
                    file_monitor_event::event_type type = file_monitor_event::null;
                    switch( fni->Action ) {
						case FILE_ACTION_ADDED: type = file_monitor_event::added; break;
						case FILE_ACTION_REMOVED: type = file_monitor_event::removed; break;
						case FILE_ACTION_MODIFIED: type = file_monitor_event::modified; break;
						case FILE_ACTION_RENAMED_OLD_NAME: type = file_monitor_event::renamed_old; break;
						case FILE_ACTION_RENAMED_NEW_NAME: type = file_monitor_event::renamed_new; break;
                    }
                    pushback_event( file_monitor_event( boost::filesystem::path( ck->path ) / to_utf8( fni->FileName, fni->FileNameLength / sizeof( WCHAR ) ), type ) );
                    offset += fni->NextEntryOffset;
                }
                while( fni->NextEntryOffset );

                ZeroMemory( &ck->overlapped, sizeof( ck->overlapped ) );
				// TODO change 0x1FF to a map that makes actual sense
				DWORD filter = 0x1FF;
                BOOL res = ReadDirectoryChangesW( ck->handle, ck->buffer, sizeof( ck->buffer ), FALSE, filter, &bytes_transferred, &ck->overlapped, NULL );
                if( !res )
                {
                    delete ck;
                    DWORD last_error = GetLastError();
					// TOOD move out of boost and into something more cinder specific
                    boost::system::system_error e( boost::system::error_code( last_error, boost::system::get_system_category() ), "boost::asio::basic_dir_monitor_service::work_thread: ReadDirectoryChangesW failed" );
                    boost::throw_exception( e );
                }
            }
        }
	}
	
	bool running()
	{
		// Access to run_ is sychronized with stop_work_thread().
		std::lock_guard<std::mutex> lock( work_thread_mutex_ );
		return run_;
	}
	
	void stop_work_thread()
	{
		// Access to run_ is sychronized with running().
		std::lock_guard<std::mutex> lock( work_thread_mutex_ );
		run_ = false;
	}

    std::string to_utf8(WCHAR *filename, DWORD length)
    {
		// TODO make this more cinder-like
        int size = WideCharToMultiByte(CP_UTF8, 0, filename, length, NULL, 0, NULL, NULL);
        if (!size)
        {
            DWORD last_error = GetLastError();
            boost::system::system_error e(boost::system::error_code(last_error, boost::system::get_system_category()), "boost::asio::basic_dir_monitor_service::to_utf8: WideCharToMultiByte failed");
            boost::throw_exception(e);
        }

        char buffer[1024];
		std::unique_ptr<char[]> dynbuffer;
        if( size > sizeof(buffer) )
        {
            dynbuffer.reset( new char[size] );
            size = WideCharToMultiByte( CP_UTF8, 0, filename, length, dynbuffer.get(), size, NULL, NULL );
        }
        else
        {
            size = WideCharToMultiByte( CP_UTF8, 0, filename, length, buffer, sizeof(buffer), NULL, NULL );
        }

        if( !size )
        {
            DWORD last_error = GetLastError();
            boost::system::system_error e(boost::system::error_code(last_error, boost::system::get_system_category()), "boost::asio::basic_dir_monitor_service::to_utf8: WideCharToMultiByte failed");
            boost::throw_exception(e);
        }

        return dynbuffer.get() ? std::string( dynbuffer.get(), size ) : std::string( buffer, size );
    }
	
    HANDLE iocp_;

	bool run_;
	std::mutex work_thread_mutex_;
	std::thread work_thread_;

	std::mutex paths_mutex_;
	boost::unordered_set<boost::filesystem::path> paths_;

	std::mutex events_mutex_;
	std::condition_variable events_cond_;
	std::deque<file_monitor_event> events_;

};

} // filemonitor namespace
