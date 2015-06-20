#pragma once

#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <boost/bimap.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <deque>
#include <thread>
#include <unistd.h>

namespace filemonitor {

class file_monitor_impl :
public boost::enable_shared_from_this<file_monitor_impl>
{
	
public:
	file_monitor_impl()
		: kqueue_( init_kqueue() ),
		run_(true),
		work_thread_( &file_monitor_impl::work_thread, this )
	{}
	
	~file_monitor_impl()
	{
		// The work thread is stopped and joined.
		stop_work_thread();
		work_thread_.join();
		::close( kqueue_ );
	}
	
	void add_file( const boost::filesystem::path &file, int event_fd )
	{
		std::lock_guard<std::mutex> lock( add_remove_mutex_ );
		add_queue_.push_back( std::pair<boost::filesystem::path, int>( file, event_fd ) );
	}
	
	void remove_file( const boost::filesystem::path &file )
	{
		std::lock_guard<std::mutex> lock( add_remove_mutex_ );
		remove_queue_.push_back( file );
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
	int init_kqueue()
	{
		int fd = kqueue();
		if( fd == -1 ) {
			boost::system::system_error e(boost::system::error_code(errno, boost::system::get_system_category()), "boost::asio::file_monitor_impl::init_kqueue: kqueue failed");
			boost::throw_exception(e);
		}
		return fd;
	}
	
	void work_thread()
	{
		while( running() ) {

			// deal with removes
			{
				std::lock_guard<std::mutex> lock( add_remove_mutex_ );
				for( const auto& name : remove_queue_ ) {
					
					auto it = files_bimap_.left.find( name );
					if( it != files_bimap_.left.end() ) {
						::close( it->second );
						files_bimap_.left.erase( name );
					}
				}
				remove_queue_.clear();
			}

			// deal with adds
			int add_index = 0;
			{
				std::lock_guard<std::mutex> lock( add_remove_mutex_ );
				
				while( ! add_queue_.empty() && add_index < event_list_size ) {
					
					int fd = add_queue_.begin()->second;
					const boost::filesystem::path& path = add_queue_.begin()->first;
					
					unsigned eventFilter = NOTE_WRITE | NOTE_DELETE | NOTE_RENAME;
					EV_SET( &event_list_[add_index++], fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, eventFilter, 0, 0 );
					
					// if user is re-adding a file, close the old handle and use the new handle.
					// combinations of rename/delete/etc could mean our filename->fd is out of date,
					// so use the new one
					auto it = files_bimap_.left.find( path );
					if( it != files_bimap_.left.end() ) {
						::close( it->second );
						bool success = files_bimap_.left.replace_data( it, fd );
						assert( success );
					} else {
						// otherwise just add
						files_bimap_.insert( watched_file( path, fd ) );
					}

					add_queue_.pop_front();
				}
			}
			
			struct timespec timeout;
			timeout.tv_sec = 0;
			timeout.tv_nsec = 200000000;
			
			int nEvents = kevent( kqueue_, event_list_, add_index, event_list_, event_list_size, &timeout );
			
			if( nEvents < 0 or event_list_[0].flags == EV_ERROR )
			{
				boost::system::system_error e(boost::system::error_code( errno, boost::system::get_system_category() ), "boost::asio::file_monitor_impl::work_thread: kevent failed");
				boost::throw_exception(e);
			}
			
			if( nEvents > 0 ) {
				for( int i=0; i<nEvents; ++i ) {
					boost::filesystem::path path( files_bimap_.right.at( event_list_[i].ident ) );
					file_monitor_event::event_type type = file_monitor_event::null;
					
					if( event_list_[i].fflags & NOTE_WRITE ) {
						type = file_monitor_event::write;
					} else if( event_list_[i].fflags & NOTE_DELETE ) {
						// TODO should we auto-remove it from our structure?  can't recover from a delete
						type = file_monitor_event::remove;
					} else if( event_list_[i].fflags & NOTE_RENAME ) {
						type = file_monitor_event::rename;
					}
					pushback_event( file_monitor_event( path, type ) );
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
	
	int kqueue_;
	bool run_;
	std::mutex work_thread_mutex_;
	std::thread work_thread_;
	
	// need to go from unix_handle.id -> path and path -> unix_handle
	typedef boost::bimap< boost::filesystem::path, int > files_bimap;
	typedef files_bimap::value_type watched_file;
	files_bimap files_bimap_;

	// for adding and removing outside of the worker thread
	std::mutex add_remove_mutex_;
	std::deque< std::pair< boost::filesystem::path, int > > add_queue_;
	std::deque< boost::filesystem::path > remove_queue_;
	
	static const int event_list_size = 256;
	struct kevent event_list_[event_list_size];
	
	std::mutex events_mutex_;
	std::condition_variable events_cond_;
	std::deque< file_monitor_event > events_;
};
	
} // asio namespace





