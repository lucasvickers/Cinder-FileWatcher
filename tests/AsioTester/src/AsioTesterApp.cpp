#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"

#include "file_monitor.hpp"

using namespace ci;
using namespace ci::app;
using namespace std;

class AsioTesterApp : public App {
  public:
	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void keyDown( KeyEvent event ) override;
	void update() override;
	void draw() override;
	void shutdown();
	
	void file_event_handler( const boost::system::error_code &ec, const filemonitor::file_monitor_event &ev );
	
	boost::asio::io_service io_service;
	shared_ptr<filemonitor::file_monitor> fm;
	auto_ptr<boost::asio::io_service::work> work;

};

void AsioTesterApp::file_event_handler( const boost::system::error_code &ec, const filemonitor::file_monitor_event &ev )
{
	console() << "EC: " << ec << endl;
	console() << "EV: " << ev << endl << endl;
	
	// re-process if no error
	if( ! ec && ev.type != filemonitor::file_monitor_event::null )
	{
		// process it.. remove if it's a delete, etc
	}
	
	// add a new handler
	fm->async_monitor( boost::bind( &AsioTesterApp::file_event_handler, this, _1, _2 ) );
}

void AsioTesterApp::setup()
{
	fm = shared_ptr<filemonitor::file_monitor>( new filemonitor::file_monitor( io_service ) );
	work = auto_ptr<boost::asio::io_service::work>( new boost::asio::io_service::work( io_service ) );
	// prime the first handler
	fm->async_monitor( boost::bind( &AsioTesterApp::file_event_handler, this, _1, _2 ) );
}

void AsioTesterApp::mouseDown( MouseEvent event )
{
}

void AsioTesterApp::keyDown( KeyEvent event )
{
	if( event.getChar() == 'a' ) {
		//fm->add_file( "/tmp/lucas" );
		assert(false);
		console() << "Added." << endl;
	} else if( event.getChar() == 'r' ) {
		//fm->remove_file( "/tmp/lucas" );
		assert(false);
		console() << "Removed." << endl;
	}
}

void AsioTesterApp::update()
{
	io_service.poll();
}

void AsioTesterApp::draw()
{
	gl::clear( Color( 0, 0, 0 ) ); 
}

void AsioTesterApp::shutdown()
{
	// shutdown io
	work.reset();
}

CINDER_APP( AsioTesterApp, RendererGl )
