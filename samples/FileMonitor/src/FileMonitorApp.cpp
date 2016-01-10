#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"

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
	
	filewatcher::WatchedFile mWatchedFile;
	filewatcher::WatchedPath mWatchedPath;
	
	void scopedWatch();
	void containerTest();
};

void FileMonitorApp::setup()
{
	mWatchedFile = filewatcher::FileWatcher::watchFile( "/Users/lucasvickers/test",
													    []( const ci::fs::path& file, filewatcher::EventType type ) {
		cout << "Activity on file: " << file << " of type " << type << endl;
	} );
	
	// watchPath not yet supported, need to build out regex support
	/*
	mWatchedPath = filewatcher::FileWatcher::watchPath( "/Users/lucasvickers/test",
														"*.jpg",
														[]( const ci::fs::path& file, filewatcher::EventType type ) {
		cout << "Activity on regex watch via file: " << file << " of type " << type << endl;
	} );
	*/
	
	scopedWatch();
	containerTest();

}

void FileMonitorApp::scopedWatch()
{
	// this shouldn't get triggered after this routine returns
	filewatcher::WatchedFile scopedWatch = filewatcher::FileWatcher::watchFile( "/Users/lucasvickers/scoped",
																			   []( const ci::fs::path& file, filewatcher::EventType type ) {
	   cout << "Activity on scoped watch file: " << file << " of type " << type << endl;
	} );

}

void FileMonitorApp::containerTest()
{
	filewatcher::WatchedFileMap<std::string> fileMap;

	// TODO make this cleaner w/ a few helper routines
	fileMap.insert( std::make_pair<std::string,filewatcher::WatchedFile>( "test", filewatcher::FileWatcher::watchFile( "/Users/lucasvickers/test",
																													   nullptr ) ) );
	console() << "Path is " << fileMap["test"].getPath() << endl;
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
