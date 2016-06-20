#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Log.h"

#include <thread>

#include "FileWatcher.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class FileMonitorApp : public App {
  public:
	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void update() override;
	void draw() override;
	
	void basicTest();
	void doubleTest();
	void scopedWatchTest();
	void containerTest();
	void assignmentTest();
	void overwriteTest();
	
	// helpers
	void writeToFile( const std::string &file, const std::string &text );
	
	fs::path mTestingPath;
};

void FileMonitorApp::setup()
{
	mTestingPath = getAppPath() / "logtest";
	fs::create_directory( mTestingPath );
	
	// watchPath not yet supported, need to build out regex support
	/*
	mWatchedPath = filewatcher::FileWatcher::watchPath( "/Users/lucasvickers/test",
														"*.jpg",
														[]( const ci::fs::path& file, filewatcher::EventType type ) {
		cout << "Activity on regex watch via file: " << file << " of type " << type << endl;
	} );
	*/
	
	// enable auto polling
	filewatcher::FileWatcher::instance()->registerUpdate();
	
	basicTest();
	//doubleTest();
	//scopedWatchTest();
	//containerTest();
	//assignmentTest();
	//overwriteTest();

}

void FileMonitorApp::writeToFile( const std::string &file, const std::string &text )
{
	ofstream ofile;
	ofile.open( file );
	ofile << text;
	ofile.flush();
	ofile.close();
}

void FileMonitorApp::basicTest()
{
	CI_LOG_D( "STARTING BASIC TEST." );
	
	// touch a temp file
	fs::path target = mTestingPath / "basictest";
	writeToFile( target.string(), "start" );
	
	filewatcher::WatchedTarget watchedFile;
	
	bool trigger = false;
	watchedFile = filewatcher::FileWatcher::watchFile( target,
	   [ &trigger ]( const ci::fs::path& file, filewatcher::EventType type ) {
		   CI_LOG_V( "-- Activity on file: " << file << " of type " << type );
		   trigger = true;
	   } );
	
	// modify the temp file
	writeToFile( target.string(), "finish" );
	
	// fake sleep
	sleep(5);
	
	// poll the service to force a check before app updates
	filewatcher::FileWatcher::instance()->poll();
	
	trigger? CI_LOG_E( "BASIC TEST PASSED." ) : CI_LOG_E( "BASIC TEST FAILED." );
}

void FileMonitorApp::doubleTest()
{
	// test what happens if we watch the same file twice
}

void FileMonitorApp::scopedWatchTest()
{/*
	// this shouldn't get triggered after this routine returns
	filewatcher::WatchedTarget scopedWatch = filewatcher::FileWatcher::watchFile( "/Users/lucasvickers/scoped",
		[]( const ci::fs::path& file, filewatcher::EventType type ) {
		   cout << "Activity on scoped watch file: " << file << " of type " << type << endl;
		} );
*/
}

void FileMonitorApp::containerTest()
{/*
	filewatcher::WatchedTargetMap<std::string> fileMap;

	// TODO make this cleaner w/ a few helper routines
	fileMap.insert( std::make_pair<std::string,filewatcher::WatchedTarget>( "test", filewatcher::FileWatcher::watchFile( "/Users/lucasvickers/test",
		[]( const ci::fs::path& file, filewatcher::EventType type ) {
			cout << "Activity on scoped watch file: " << file << " of type " << type << endl;
		} );
																		   
	console() << "Path is " << fileMap["test"].getPath() << endl;*/
}

void FileMonitorApp::assignmentTest()
{
	// test what happens when we try to transfer watcher ownership
}

void FileMonitorApp::overwriteTest()
{
	// test what happens when we assign a new watch to an already assigned watcher
}
																		   
void FileMonitorApp::mouseDown( MouseEvent event )
{
}

void FileMonitorApp::update()
{
}

void FileMonitorApp::draw()
{
	gl::clear( Color( 0, 0, 0 ) ); 
}

CINDER_APP( FileMonitorApp, RendererGl )
