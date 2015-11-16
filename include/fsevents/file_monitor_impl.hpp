//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <CoreServices/CoreServices.h>

#include <boost/unordered_set.hpp>	// to remove

#include <deque>
#include <thread>
#include <time.h>

namespace filemonitor {

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
		stop_fsevents();
	}
	
	void add_file( const boost::filesystem::path &file )
	{
		std::lock_guard<std::mutex> lock( paths_mutex_ );
		paths_.insert( file );
		stop_fsevents();
		start_fsevents();
	}
	
	void remove_file( const boost::filesystem::path &file )
	{
		std::lock_guard<std::mutex> lock( paths_mutex_ );
		paths_.erase( file );
		stop_fsevents();
		start_fsevents();
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
	
	CFArrayRef make_array( const boost::unordered_set<boost::filesystem::path> &in )
	{
		CFMutableArrayRef arr = CFArrayCreateMutable( kCFAllocatorDefault, in.size(), &kCFTypeArrayCallBacks );
		for (auto str : in) {
			CFStringRef cfstr = CFStringCreateWithCString( kCFAllocatorDefault, str.c_str(), kCFStringEncodingUTF8 );
			CFArrayAppendValue( arr, cfstr );
			CFRelease(cfstr);
		}
		return arr;
	}
	
	void start_fsevents()
	{
		if ( paths_.size() == 0 ) {
			fsevents_ = nullptr;
			return;
		}
		
		FSEventStreamContext context = {0, this, NULL, NULL, NULL};
		fsevents_ = FSEventStreamCreate( kCFAllocatorDefault,
										 &filemonitor::file_monitor_impl::fsevents_callback,
										 &context,
										 make_array( paths_ ),
										 //todo determine when we need to support historical events (if ever, I hope never)
										 kFSEventStreamEventIdSinceNow, 		// only new modifications
										 (CFTimeInterval) 1.0, 					// 1 second latency interval
										 kFSEventStreamCreateFlagFileEvents );
		FSEventStreamRetain( fsevents_ );
		
		if( ! fsevents_ )
		{
			boost::system::system_error e( boost::system::error_code( errno, boost::system::get_system_category() ),
										  							  "filemonitor::file_monitor_impl::init_fsevents: fsevents failed" );
			boost::throw_exception(e);
		}
		
		while( ! runloop_ ) {
			// TODO: why yield the main(service?) thread?  Kinda useless as a one shot call.
			std::this_thread::yield();
		}
		
		FSEventStreamScheduleWithRunLoop( fsevents_, runloop_, kCFRunLoopDefaultMode );
		FSEventStreamStart( fsevents_ );
		runloop_cond_.notify_all();
		FSEventStreamFlushAsync( fsevents_ );
		
		// todo delete fsevents_ ?  Create pattern
	}
	
	void stop_fsevents()
	{
		if (fsevents_)
		{
			FSEventStreamStop( fsevents_ );
			// todo do we need to unschedule this?
			// FSEventStreamUnscheduleFromRunLoop(fsevents_, runloop_, kCFRunLoopDefaultMode);
			FSEventStreamInvalidate( fsevents_ );
			FSEventStreamRelease( fsevents_ );
		}
	}
	
	static void fsevents_callback( ConstFSEventStreamRef streamRef,
								   void *clientCallBackInfo,
								   size_t numEvents,
								   void *eventPaths,
								   const FSEventStreamEventFlags eventFlags[],
								   const FSEventStreamEventId eventIds[] )
	{
		size_t i;
		char **paths = (char**)eventPaths;
		file_monitor_impl* impl = (file_monitor_impl*)clientCallBackInfo;
		
		for( i = 0; i < numEvents; ++i )
		{
			// TODO: keep track of these, because we don't necessarily want to return folders as events
			// kFSEventStreamEventFlagItemIsDir
			// kFSEventStreamEventFlagItemIsFile
			
			boost::filesystem::path path( paths[i] );
			if( eventFlags[i] & kFSEventStreamEventFlagNone ) {
				// todo log this
			}
			if( eventFlags[i] & kFSEventStreamEventFlagMustScanSubDirs ) {
				// Events coalesced into a single event.  Docs recommend a directory scan to figure out what
				// changed.  I should log errors and see if this ever actually happens.
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemCreated ) {
				impl->pushback_event( file_monitor_event( path, file_monitor_event::added));
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRemoved ) {
				impl->pushback_event( file_monitor_event( path, file_monitor_event::removed));
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemModified ) {
				impl->pushback_event( file_monitor_event( path, file_monitor_event::modified));
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRenamed )
			{
				if( ! boost::filesystem::exists( path ) )
				{
					impl->pushback_event( file_monitor_event( path, file_monitor_event::renamed_old ) );
				}
				else
				{
					impl->pushback_event( file_monitor_event( path, file_monitor_event::renamed_new ) );
				}
			}
		}
	}
	
	void work_thread()
	{
		runloop_ = CFRunLoopGetCurrent();
		
		while( running() )
		{
			std::unique_lock<std::mutex> lock( runloop_mutex_ );
			runloop_cond_.wait( lock );
			CFRunLoopRun();
		}
	}
	
	bool running()
	{
		std::lock_guard<std::mutex> lock( work_thread_mutex_ );
		return run_;
	}
	
	void stop_work_thread()
	{
		// Access to run_ is sychronized with running().
		std::lock_guard<std::mutex> lock( work_thread_mutex_ );
		run_ = false;
		CFRunLoopStop( runloop_ ); // exits the thread
		runloop_cond_.notify_all();
	}
	
	bool run_{false};
	CFRunLoopRef runloop_;
	std::mutex runloop_mutex_;
	std::condition_variable runloop_cond_;

	std::mutex work_thread_mutex_;
	std::thread work_thread_;
	
	FSEventStreamRef fsevents_;
	
	std::mutex paths_mutex_;
	boost::unordered_set<boost::filesystem::path> paths_;
	
	std::mutex events_mutex_;
	std::condition_variable events_cond_;
	std::deque<file_monitor_event> events_;
};

} // asio namespace
