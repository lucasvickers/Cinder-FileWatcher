#pragma once

#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <boost/ptr_container/ptr_unordered_map.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <thread>
#include <unistd.h>

namespace boost {
namespace asio {

class file_monitor_impl :
public boost::enable_shared_from_this<file_monitor_impl>
{
	class unix_handle
	: public boost::noncopyable
	{
	public:
		unix_handle( int handle )
		: handle_(handle)
		{
		}
		
		~unix_handle()
		{
			::close( handle_ );
		}
		
		operator int() const { return handle_; }
		
	private:
		int handle_;
	};
	
public:
	file_monitor_impl()
		: kqueue_( init_kqueue() ),
		run_(true),
		work_thread_( &boost::asio::file_monitor_impl::work_thread, this )
	{}
	
	~file_monitor_impl()
	{
		// The work thread is stopped and joined.
		stop_work_thread();
		work_thread_.join();
		::close( kqueue_ );
	}
	
	void add_file( std::string filename, int event_fd )
	{
		// LV TODO add it to the add queue
		
		boost::unique_lock<boost::mutex> lock( files_mutex_ );
		files_.insert( filename, new unix_handle( event_fd ) );
	}
	
	void remove_file( const std::string &filename )
	{
		// LV TODO add it to the remove queue
		boost::unique_lock<boost::mutex> lock( files_mutex_ );
		files_.erase( filename );
	}
	
	void destroy()
	{
		boost::unique_lock<boost::mutex> lock( events_mutex_ );
		run_ = false;
		events_cond_.notify_all();
	}

	file_monitor_event popfront_event( boost::system::error_code &ec )
	{
		boost::unique_lock<boost::mutex> lock( events_mutex_ );
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
	
	void pushback_event( const file_monitor_event& ev )
	{
		boost::unique_lock<boost::mutex> lock( events_mutex_ );
		if( run_ ) {
			events_.push_back( ev );
			events_cond_.notify_all();
		}
	}
	
private:
	int init_kqueue()
	{
		int fd = kqueue();
		if (fd == -1)
		{
			boost::system::system_error e(boost::system::error_code(errno, boost::system::get_system_category()), "boost::asio::file_monitor_impl::init_kqueue: kqueue failed");
			boost::throw_exception(e);
		}
		return fd;
	}
	
	void work_thread()
	{
		while( running() ) {
			for( auto file : files_ ) {
				
			}
			
			/*
			for ( auto dir : dirs_ ) {
			
				struct timespec timeout;
				timeout.tv_sec = 0;
				timeout.tv_nsec = 200000000;
				unsigned eventFilter = NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_EXTEND | NOTE_ATTRIB;
				struct kevent event;
				struct kevent eventData;
				EV_SET(&event, *dir->second, EVFILT_VNODE, EV_ADD | EV_CLEAR, eventFilter, 0, 0);
				int nEvents = kevent(kqueue_, &event, 1, &eventData, 1, &timeout);
				
				if (nEvents < 0 or eventData.flags == EV_ERROR)
				{
					boost::system::system_error e(boost::system::error_code(errno, boost::system::get_system_category()), "boost::asio::dir_monitor_impl::work_thread: kevent failed");
					boost::throw_exception(e);
				}
				
				// dir_monitor_event::event_type type = dir_monitor_event::null;
				// if (eventData.fflags & NOTE_WRITE) {
				//     type = dir_monitor_event::modified;
				// }
				// else if (eventData.fflags & NOTE_DELETE) {
				//     type = dir_monitor_event::removed;
				// }
				// else if (eventData.fflags & NOTE_RENAME) {
				//     type = dir_monitor_event::renamed_new_name;
				// case FILE_ACTION_RENAMED_OLD_NAME: type = dir_monitor_event::renamed_old_name; break;
				// case FILE_ACTION_RENAMED_NEW_NAME: type = dir_monitor_event::renamed_new_name; break;
				// }
				
				// Run recursive directory check to find changed files
				// Similar to Poco's DirectoryWatcher
				// @todo Use FSEvents API on OSX?
				
				dir_entry_map new_entries;
				scan(dir->first, new_entries);
				compare(dir->first, entries[dir->first], new_entries);
				std::swap(entries[dir->first], new_entries);
			}
			 */
		}
	}
	
	bool running()
	{
		// Access to run_ is sychronized with stop_work_thread().
		boost::mutex::scoped_lock lock( work_thread_mutex_ );
		return run_;
	}
	
	void stop_work_thread()
	{
		// Access to run_ is sychronized with running().
		boost::mutex::scoped_lock lock( work_thread_mutex_ );
		run_ = false;
	}
	
	int kqueue_;
	bool run_;
	boost::mutex work_thread_mutex_;
	std::thread work_thread_;
	
	// LV TODO see if you can remove the files_mutex_
	boost::mutex add_remove_mutex_;
	boost::ptr_unordered_map<std::string, unix_handle> files_;
	//boost::ptr_unordered_map<std::string, unix_handle> files_;
	
	struct kevent eventlist_[256];
	
	boost::mutex events_mutex_;
	boost::condition_variable events_cond_;
	std::deque<file_monitor_event> events_;
};
	
} // asio namespace
} // boost namespace





