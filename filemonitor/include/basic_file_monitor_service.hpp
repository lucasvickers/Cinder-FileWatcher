#pragma once

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  include "windows/file_monitor_impl.hpp"
#elif defined(__APPLE__) && defined(__MACH__)
#  include "fsevents/file_monitor_impl.hpp"
#else
// fallback method
#  include "polling/file_monitor_impl.hpp"
#endif


#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/scoped_ptr.hpp>			// TODO check need of scoped_ptr

// TODO move to cinder's asio
//#include "asio/asio.hpp"

namespace filemonitor {

template <typename FileMonitorImplementation = file_monitor_impl>
class basic_file_monitor_service
: public boost::asio::io_service::service
{
public:
	static boost::asio::io_service::id id;
	
	explicit basic_file_monitor_service( boost::asio::io_service &io_service )
	: boost::asio::io_service::service( io_service ),
	async_monitor_work_( new boost::asio::io_service::work( async_monitor_io_service_ ) ),
	async_monitor_thread_( boost::bind( &boost::asio::io_service::run, &async_monitor_io_service_ ) )
	{
	}
	
	~basic_file_monitor_service()
	{
		// The async_monitor thread will finish when mAsync_monitor_work is reset as all asynchronous
		// operations have been aborted and were discarded before (in destroy).
		async_monitor_work_.reset();
		
		// Event processing is stopped to discard queued operations.
		async_monitor_io_service_.stop();
		
		// The async_monitor thread is joined to make sure the file monitor service is
		// destroyed _after_ the thread is finished (not that the thread tries to access
		// instance properties which don't exist anymore).
		async_monitor_thread_.join();
	}
	
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
	
	uint64_t add_path( implementation_type &impl, const boost::filesystem::path &path, const std::string& regex_match )
	{
		if ( ! boost::filesystem::is_directory( path ) ) {
			throw std::invalid_argument("boost::asio::basic_file_monitor_service::add_file: \"" +
										path.string() + "\" is not a valid file or directory entry");
		}
		return impl->add_path( path, regex_match );
	}
	
	uint64_t add_file( implementation_type &impl, const boost::filesystem::path &path )
	{
		if ( ! boost::filesystem::is_regular_file( path ) ) {
			throw std::invalid_argument("boost::asio::basic_file_monitor_service::add_file: \"" +
										path.string() + "\" is not a valid file or directory entry");
		}
		return impl->add_file( path );
	}

	void remove( implementation_type &impl, uint64_t id )
	{
		impl->remove( id );
	}
	
	/**
	 * Blocking event monitor.
	 */
	file_monitor_event monitor( implementation_type &impl, boost::system::error_code &ec )
	{
		return impl->popfront_event( ec );
	}
	
	template <typename Handler>
	class monitor_operation
	{
	public:
		monitor_operation( implementation_type &impl, boost::asio::io_service &io_service, Handler handler )
		: impl_( impl ),
		io_service_( io_service ),
		work_( io_service ),
		handler_( handler )
		{
		}
		
		void operator()() const
		{
			implementation_type impl = impl_.lock();
			if( impl ) {
				boost::system::error_code ec;
				file_monitor_event ev = impl->popfront_event( ec );
				this->io_service_.post( boost::asio::detail::bind_handler( handler_, ec, ev ) );
			}
			else {
				this->io_service_.post( boost::asio::detail::bind_handler( handler_,
																	   boost::asio::error::operation_aborted,
																	   file_monitor_event() ) );
			}
		}
		
	private:
		boost::weak_ptr<FileMonitorImplementation> 	impl_;
		boost::asio::io_service 					&io_service_;
		boost::asio::io_service::work 				work_;
		Handler 									handler_;
	};
	
	/**
	 * Non-blocking event monitor.
	 */
	template <typename Handler>
	void async_monitor( implementation_type &impl, Handler handler )
	{
		this->async_monitor_io_service_.post( monitor_operation<Handler>( impl, this->get_io_service(), handler ) );
	}
	
private:
	void shutdown_service()
	{
	}
	
	boost::asio::io_service 							async_monitor_io_service_;
	boost::scoped_ptr<boost::asio::io_service::work> 	async_monitor_work_;
	std::thread 										async_monitor_thread_;
};

template <typename FileMonitorImplementation>
boost::asio::io_service::id basic_file_monitor_service<FileMonitorImplementation>::id;

} // filemonitor namespace





