#pragma once

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <string>

namespace filemonitor {

struct FileMonitorEvent
{
	enum EventType
	{
		NONE,
		REMOVED,		// file removed
		ADDED,			// file added
		MODIFIED,		// file changed
		RENAMED_OLD,	// file renamed, old name
		RENAMED_NEW		// file renamed, new name
	};
	
	FileMonitorEvent()
	: type( NONE ), id( 0 )
	{ }
	
	FileMonitorEvent( const boost::filesystem::path &p, EventType t, uint64_t id )
	: path( p ), type( t ), id( id )
	{ }
	
	boost::filesystem::path path;
	EventType 				type;
	uint64_t 				id;
};

inline std::ostream& operator << ( std::ostream& os, const FileMonitorEvent &ev )
{
	os << "FileMonitorEvent "
	<< []( int type ) {
		switch( type ) {
			case FileMonitorEvent::REMOVED: return "REMOVED";
			case FileMonitorEvent::ADDED: return "ADDED";
			case FileMonitorEvent::MODIFIED: return "MODIFIED";
			case FileMonitorEvent::RENAMED_OLD: return "RENAMED_OLD";
			case FileMonitorEvent::RENAMED_NEW: return "RENAMED_NEW";
			default: return "UNKNOWN";
		}
	} ( ev.type ) << " " << ev.path;
	return os;
}

template <typename Service>
class BasicFileMonitor
	: public boost::asio::basic_io_object<Service>
{
public:
	explicit BasicFileMonitor( boost::asio::io_service &io_service )
		: boost::asio::basic_io_object<Service>( io_service )
	{
	}
	
	uint64_t addFile( const boost::filesystem::path &file )
	{
		return this->service.addFile( this->implementation, file );
	}
	
	uint64_t addPath( const boost::filesystem::path &path, const std::string &regexMatch )
	{
		return this->service.addPath( this->implementation, path, regexMatch );
	}
	
	void remove( uint64_t id )
	{
		this->service.remove( this->implementation, id );
	}
	
	FileMonitorEvent monitor()
	{
		boost::system::error_code ec;
		FileMonitorEvent ev = this->service.monitor( this->implementation, ec );
		boost::asio::detail::throw_error( ec );
		return ev;
	}
	
	FileMonitorEvent monitor( boost::system::error_code &ec )
	{
		return this->service.monitor( this->implementation, ec );
	}
	
	template <typename Handler>
	void asyncMonitor( Handler handler )
	{
		this->service.asyncMonitor( this->implementation, handler );
	}
};
	
}
