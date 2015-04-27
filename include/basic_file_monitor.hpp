#pragma once

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <string>

namespace boost {
namespace asio {

struct file_monitor_event
{
	enum event_type
	{
		null = 0,
		remove = 1,		// removed
		write = 2,		// contents changed
		extend = 3,		// size increased
		rename = 4		// renamed
	};
	
	file_monitor_event()
	: type( null ) { }
	
	file_monitor_event( const boost::filesystem::path &p, event_type t )
	: path( p ), type( t ) { }
	
	boost::filesystem::path path;
	event_type type;
};

inline std::ostream& operator << ( std::ostream& os, const file_monitor_event &ev )
{
	os << "file_monitor_event "
	<< []( int type ) {
		switch(type) {
			case boost::asio::file_monitor_event::remove: return "ADDED";
			case boost::asio::file_monitor_event::write: return "REMOVED";
			case boost::asio::file_monitor_event::extend: return "MODIFIED";
			case boost::asio::file_monitor_event::rename: return "RENAMED";
				// LVTODO: see about the new/old name stuff
			default: return "UNKNOWN";
		}
	} ( ev.type ) << " " << ev.path;
	return os;
}

template <typename Service>
class basic_file_monitor
	: public boost::asio::basic_io_object<Service>
{
public:
	explicit basic_file_monitor( boost::asio::io_service &io_service )
		: boost::asio::basic_io_object<Service>( io_service )
	{
	}
	
	void add_file( const std::string &filename )
	{
		this->service.add_file( this->implementation, filename );
	}
	
	void remove_file( const std::string &filename )
	{
		this->service.remove_file( this->implementation, filename );
	}
	
	file_monitor_event monitor()
	{
		boost::system::error_code ec;
		file_monitor_event ev = this->service.monitor(this->implementation, ec);
		boost::asio::detail::throw_error(ec);
		return ev;
	}
	
	file_monitor_event monitor( boost::system::error_code &ec )
	{
		return this->service.monitor(this->implementation, ec);
	}
	
	template <typename Handler>
	void async_monitor( Handler handler )
	{
		this->service.async_monitor(this->implementation, handler);
	}
};
	
}
}