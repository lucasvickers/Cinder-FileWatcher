#pragma once

#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <deque>
#include <thread>
#include <time.h>

namespace filemonitor {
	
	static const uint16_t POLLING_DELAY = 200;
	
	class file_monitor_impl :
	public boost::enable_shared_from_this<file_monitor_impl>
	{
		
	public:
		file_monitor_impl()
		: run_(true),
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
			std::lock_guard<std::mutex> lock( add_remove_mutex_ );
			add_queue_.push_back( file );
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
		
		void work_thread()
		{
			while( running() ) {
		
				// deal with removes
				{
					std::lock_guard<std::mutex> lock( add_remove_mutex_ );
					for( const auto& name : remove_queue_ ) {
						file_timestamps_.erase( name );
					}
					remove_queue_.clear();
				}
				
				// deal with adds
				{
					std::lock_guard<std::mutex> lock( add_remove_mutex_ );
					for( const auto& file : add_queue_ ) {
						// overwrite existing items to re-fresh the timestamp
						file_timestamps_[file] = boost::filesystem::last_write_time( file );
					}
					add_queue_.clear();
				}
				
				// process timestamps
				for( const auto& it : file_timestamps_ ) {
					// check if it still exists
					if( ! boost::filesystem::exists( it.first ) ) {
						// renamed or deleted, assume deleted
						pushback_event( file_monitor_event( it.first, file_monitor_event::remove ) );
						{
							std::lock_guard<std::mutex> lock( add_remove_mutex_ );
							remove_queue_.push_back( it.first );
						}
					} else if ( boost::filesystem::last_write_time( it.first ) != it.second ) {
						// file modified/written
						pushback_event( file_monitor_event( it.first, file_monitor_event::write ) );
						file_timestamps_[it.first] = boost::filesystem::last_write_time( it.first );
					}
				}
				
				std::this_thread::sleep_for( std::chrono::milliseconds( POLLING_DELAY ) );
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
		
		bool run_;
		std::mutex work_thread_mutex_;
		std::thread work_thread_;
		
		// filepath -> timestamp
		std::map<boost::filesystem::path, std::time_t> file_timestamps_;
		
		// for adding and removing outside of the worker thread
		std::mutex add_remove_mutex_;
		std::deque<boost::filesystem::path> add_queue_;
		std::deque<boost::filesystem::path> remove_queue_;
		
		std::mutex events_mutex_;
		std::condition_variable events_cond_;
		std::deque< file_monitor_event > events_;
	};
	
} // asio namespace





