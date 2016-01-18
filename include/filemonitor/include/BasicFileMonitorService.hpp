#pragma once

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include "windows/FileMonitorImpl.hpp"
#elif defined(__APPLE__) && defined(__MACH__)
#  include "fsevents/FileMonitorImpl.hpp"
#else
// fallback method
#  include "polling/FileMonitorImpl.hpp"
#endif


#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>

// TODO move to cinder's asio
//#include "asio/asio.hpp"

namespace filemonitor {

template <typename FileMonitorImplementation = FileMonitorImpl>
class BasicFileMonitorService
: public boost::asio::io_service::service
{
public:
	static boost::asio::io_service::id id;
	
	explicit BasicFileMonitorService( boost::asio::io_service &io_service )
	: boost::asio::io_service::service( io_service ),
	mAsyncMonitorWork( new boost::asio::io_service::work( mAsyncMonitorIoService ) ),
	mAsyncMonitorThread( boost::bind( &boost::asio::io_service::run, &mAsyncMonitorIoService ) )
	{
	}
	
	~BasicFileMonitorService()
	{
		// The asyncMonitor thread will finish when mAsyncMonitorWork is reset as all asynchronous
		// operations have been aborted and were discarded before (in destroy).
		mAsyncMonitorWork.reset();
		
		// Event processing is stopped to discard queued operations.
		mAsyncMonitorIoService.stop();
		
		// The asyncMonitor thread is joined to make sure the file monitor service is
		// destroyed _after_ the thread is finished (not that the thread tries to access
		// instance properties which don't exist anymore).
		mAsyncMonitorThread.join();
	}
	
	// TODO move to std
	typedef boost::shared_ptr<FileMonitorImplementation> implementation_type;
	
	void construct( implementation_type &impl )
	{
		impl.reset( new FileMonitorImplementation() );
	}
	
	void destroy( implementation_type &impl )
	{
		// If an asynchronous call is currently waiting for an event
		// we must interrupt the blocked call to make sure it returns.
		impl->destroy();
		
		impl.reset();
	}
	
	uint64_t addPath( implementation_type &impl, const boost::filesystem::path &path, const std::string& regexMatch )
	{
		if ( ! boost::filesystem::is_directory( path ) ) {
			throw std::invalid_argument("boost::asio::BasicFileMonitorService::addFile: \"" +
										path.string() + "\" is not a valid file or directory entry");
		}
		return impl->addPath( path, regexMatch );
	}
	
	uint64_t addFile( implementation_type &impl, const boost::filesystem::path &path )
	{
		if ( ! boost::filesystem::is_regular_file( path ) ) {
			throw std::invalid_argument("boost::asio::BasicFileMonitorService::addFile: \"" +
										path.string() + "\" is not a valid file or directory entry");
		}
		return impl->addFile( path );
	}

	void remove( implementation_type &impl, uint64_t id )
	{
		impl->remove( id );
	}
	
	/**
	 * Blocking event monitor.
	 */
	FileMonitorEvent monitor( implementation_type &impl, boost::system::error_code &ec )
	{
		return impl->popFrontEvent( ec );
	}
	
	template <typename Handler>
	class MonitorOperation
	{
	public:
		MonitorOperation( implementation_type &impl, boost::asio::io_service &ioService, Handler handler )
		: mImpl( impl ),
		mIoService( ioService ),
		mWork( ioService ),
		mHandler( handler )
		{
		}
		
		void operator()() const
		{
			implementation_type impl = mImpl.lock();
			if( impl ) {
				boost::system::error_code ec;
				FileMonitorEvent ev = impl->popFrontEvent( ec );
				this->mIoService.post( boost::asio::detail::bind_handler( mHandler, ec, ev ) );
			}
			else {
				this->mIoService.post( boost::asio::detail::bind_handler( mHandler,
																	   boost::asio::error::operation_aborted,
																	   FileMonitorEvent() ) );
			}
		}
		
	private:
		boost::weak_ptr<FileMonitorImplementation> 	mImpl;
		boost::asio::io_service 					&mIoService;
		boost::asio::io_service::work 				mWork;
		Handler 									mHandler;
	};
	
	/**
	 * Non-blocking event monitor.
	 */
	template <typename Handler>
	void asyncMonitor( implementation_type &impl, Handler handler )
	{
		this->mAsyncMonitorIoService.post( MonitorOperation<Handler>( impl, this->get_io_service(), handler ) );
	}
	
private:
	void shutdown_service()
	{
		//TODO need anything?
	}
	
	boost::asio::io_service 							mAsyncMonitorIoService;
	//! note: migrated from scoped_ptr.  remove comment if this works
	std::unique_ptr<boost::asio::io_service::work> 		mAsyncMonitorWork;
	std::thread 										mAsyncMonitorThread;
};

template <typename FileMonitorImplementation>
boost::asio::io_service::id BasicFileMonitorService<FileMonitorImplementation>::id;

} // filemonitor namespace





