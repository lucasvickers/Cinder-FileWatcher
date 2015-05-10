#pragma once

#include "file_monitor_impl.hpp"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/scoped_ptr.hpp>

namespace boost {
namespace asio {

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

	void add_file( implementation_type &impl, const std::string &filename )
	{
		if ( ! boost::filesystem::is_regular_file( filename ) ) {
			throw std::invalid_argument("boost::asio::basic_file_monitor_service::add_file: " +
										filename + " is not a valid file entry");
		}
		
		int event_fd = ::open(filename.c_str(), O_EVTONLY);
		if( event_fd < 0 ) {
			boost::system::system_error e( boost::system::error_code( errno, boost::system::get_system_category() ),
										   "boost::asio::file_monitor_impl::add_file: open failed" );
			boost::throw_exception(e);
		}
		
		impl->add_file(filename, event_fd);
	}
	
	void remove_file( implementation_type &impl, const std::string &filename )
	{
		// Removing the file from the implementation will automatically close the associated file handle.
		// Closing the file handle will make kevent() clear corresponding events.
		impl->remove_file( filename );
	}
	
	/**
	 * Blocking event monitor.
	 */
	file_monitor_event monitor( implementation_type &impl, boost::system::error_code &ec )
	{
		return impl->popfront_event(ec);
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
			if (impl) {
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
		boost::weak_ptr<FileMonitorImplementation> impl_;
		boost::asio::io_service &io_service_;
		boost::asio::io_service::work work_;
		Handler handler_;
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
	
	boost::asio::io_service async_monitor_io_service_;
	boost::scoped_ptr<boost::asio::io_service::work> async_monitor_work_;
	std::thread async_monitor_thread_;
};
	
template <typename FileMonitorImplementation>
boost::asio::io_service::id basic_file_monitor_service<FileMonitorImplementation>::id;

} // asio namespace
} // boost namespace





